import platform;
import shutil;
import os;
import glob;
import atexit;
import codecs;
import subprocess;
import sys
import errno;
from datetime import datetime,timedelta;

srcPath = ''
SettingsPath = ''
tmpSettingsDir = ''
testSettings.logScreenshotOnFail = True
testSettings.logScreenshotOnError = True
__origStartApplication__ = None

source("../../shared/classes.py")
source("../../shared/utils.py")
source("../../shared/fs_utils.py")
source("../../shared/build_utils.py")
source("../../shared/project.py")
source("../../shared/editor_utils.py")
source("../../shared/project_explorer.py")
source("../../shared/hook_utils.py")
source("../../shared/debugger.py")
source("../../shared/workarounds.py")

# ATTENTION: if a test case calls startApplication("qtcreator...") for several times this
# function must be called BEFORE any call except the first (which is done always automatically)
def overrideStartApplication():
    global startApplication, __origStartApplication__
    if (platform.system() != "Darwin"):
        return
    if (__origStartApplication__ == None):
        __origStartApplication__ = startApplication
    def startApplication(*args):
        args = list(args)
        if str(args[0]).startswith('qtcreator'):
            args[0] = args[0].replace('qtcreator', '"Qt Creator"', 1)
            test.log("Using workaround for MacOS (different AUT name)")
        return __origStartApplication__(*args)

def startedWithoutPluginError():
    try:
        loaderErrorWidgetName = ("{name='ExtensionSystem__Internal__PluginErrorOverview' "
                                 "type='ExtensionSystem::PluginErrorOverview' visible='1' "
                                 "windowTitle='Qt Creator - Plugin loader messages'}")
        loaderError = waitForObject(loaderErrorWidgetName, 1000)
        test.fatal("Could not perform clean start of Qt Creator - Plugin error occurred.",
                   waitForObject("{name='pluginError' type='QTextEdit' visible='1' window=%s}"
                                 % loaderErrorWidgetName, 1000).plainText)
        clickButton("{text~='(Next.*|Continue)' type='QPushButton' visible='1'}")
        invokeMenuItem("File", "Exit")
        return False
    except:
        return True

def waitForCleanShutdown(timeOut=10):
    appCtxt = currentApplicationContext()
    shutdownDone = (str(appCtxt)=="")
    if platform.system() in ('Windows','Microsoft'):
        endtime = datetime.utcnow() + timedelta(seconds=timeOut)
        while not shutdownDone:
            # following work-around because os.kill() works for win not until python 2.7
            if appCtxt.pid==-1:
                break
            tasks = subprocess.Popen("tasklist /FI \"PID eq %d\"" % appCtxt.pid, shell=True,stdout=subprocess.PIPE)
            output = tasks.communicate()[0]
            tasks.stdout.close()
            if (output=="INFO: No tasks are running which match the specified criteria."
                or output=="" or output.find("ERROR")==0):
                shutdownDone=True
            if not shutdownDone and datetime.utcnow() > endtime:
                break
    else:
        endtime = datetime.utcnow() + timedelta(seconds=timeOut)
        while not shutdownDone:
            try:
                os.kill(appCtxt.pid,0)
            except OSError, err:
                if err.errno == errno.EPERM or err.errno == errno.ESRCH:
                    shutdownDone=True
            if not shutdownDone and datetime.utcnow() > endtime:
                break

def __removeTestingDir__():
    def __removeIt__(directory):
        deleteDirIfExists(directory)
        return not os.path.exists(directory)

    devicesXML = os.path.join(tmpSettingsDir, "QtProject", "qtcreator", "devices.xml")
    lastMTime = os.path.getmtime(devicesXML)
    testingDir = os.path.dirname(os.path.dirname(tmpSettingsDir))
    waitForCleanShutdown()
    waitFor('os.path.getmtime(devicesXML) > lastMTime', 5000)
    waitFor('__removeIt__(testingDir)', 2000)

def substituteTildeWithinToolchains(settingsDir):
    toolchains = os.path.join(settingsDir, "QtProject", 'qtcreator', 'toolchains.xml')
    origToolchains = toolchains + "_orig"
    home = os.path.expanduser("~")
    os.rename(toolchains, origToolchains)
    origFile = open(origToolchains, "r")
    modifiedFile = open(toolchains, "w")
    for line in origFile:
        if "~" in line:
            line = line.replace("~", home)
        modifiedFile.write(line)
    origFile.close()
    modifiedFile.close()
    os.remove(origToolchains)
    test.log("Substituted all tildes with '%s' inside toolchains.xml..." % home)

def __guessABI__(supportedABIs, use64Bit):
    if use64Bit:
        searchFor = "64bit"
    else:
        searchFor = "32bit"
    for abi in supportedABIs:
        if searchFor in abi:
            return abi
    if use64Bit:
        test.log("Supported ABIs do not include an ABI supporting 64bit - trying 32bit now")
        return __guessABI__(supportedABIs, False)
    test.fatal('Could not guess ABI!',
               'Given ABIs: %s' % str(supportedABIs))
    return ''

def __is64BitOS__():
    if platform.system() == 'Darwin':
        return sys.maxsize > (2 ** 32)
    if platform.system() in ('Microsoft', 'Windows'):
        machine = os.getenv("PROCESSOR_ARCHITEW6432", os.getenv("PROCESSOR_ARCHITECTURE"))
    else:
        machine = platform.machine()
    if machine:
        return '64' in machine
    else:
        return False

def substituteUnchosenTargetABIs(settingsDir):
    class ReadState:
        NONE = 0
        READING = 1
        CLOSED = 2

    on64Bit = __is64BitOS__()
    toolchains = os.path.join(settingsDir, "QtProject", 'qtcreator', 'toolchains.xml')
    origToolchains = toolchains + "_orig"
    os.rename(toolchains, origToolchains)
    origFile = open(origToolchains, "r")
    modifiedFile = open(toolchains, "w")
    supported = []
    readState = ReadState.NONE
    for line in origFile:
        if readState == ReadState.NONE:
            if "SupportedAbis" in line:
                supported = []
                readState = ReadState.READING
        elif readState == ReadState.READING:
            if "</valuelist>" in line:
                readState = ReadState.CLOSED
            else:
                supported.append(line.split(">", 1)[1].rsplit("<", 1)[0])
        elif readState == ReadState.CLOSED:
            if "SupportedAbis" in line:
                supported = []
                readState = ReadState.READING
            elif "SET_BY_SQUISH" in line:
                line = line.replace("SET_BY_SQUISH", __guessABI__(supported, on64Bit))
        modifiedFile.write(line)
    origFile.close()
    modifiedFile.close()
    os.remove(origToolchains)
    test.log("Substituted unchosen ABIs inside toolchains.xml...")

def copySettingsToTmpDir():
    global tmpSettingsDir
    global SettingsPath
    tmpSettingsDir = tempDir()
    tmpSettingsDir = os.path.abspath(tmpSettingsDir + "/settings")
    shutil.copytree(cwd, tmpSettingsDir)
    if platform.system() in ('Linux', 'Darwin'):
        substituteTildeWithinToolchains(tmpSettingsDir)
    substituteUnchosenTargetABIs(tmpSettingsDir)
    SettingsPath = ' -settingspath "%s"' % tmpSettingsDir

if platform.system() in ('Windows', 'Microsoft'):
    sdkPath = "C:\\QtSDK"
    cwd = os.getcwd()       # current dir is directory holding qtcreator.py
    cwd+="\\..\\..\\settings\\windows"
    defaultQtVersion = "Qt 4.7.4 for Desktop - MinGW 4.4 (Qt SDK)"
else:
    sdkPath = os.path.expanduser("~/QtSDK")
    cwd = os.getcwd()       # current dir is directory holding qtcreator.py
    cwd+="/../../settings/unix"
    defaultQtVersion = "Desktop Qt 4.7.4 for GCC (Qt SDK)"
srcPath = os.getenv("SYSTEST_SRCPATH", sdkPath + "/src")

overrideStartApplication()

# the following only doesn't work if the test ends in an exception
if os.getenv("SYSTEST_NOSETTINGSPATH") != "1":
    cwd = os.path.abspath(cwd)
    copySettingsToTmpDir()
    atexit.register(__removeTestingDir__)
