/**
 * POC implementation of an electric buzzer over AllJoyn for the Raspberry Pi.
 * (c) Infragistics, 2015.
 */

#include <qcc/platform.h>

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
#include <vector>

#include <qcc/String.h>

#include <alljoyn/audio/Audio.h>
#include <alljoyn/audio/StreamObject.h>
#include <alljoyn/audio/posix/ALSADevice.h>
#include <alljoyn/AllJoynStd.h>
#include <alljoyn/BusAttachment.h>
#include <alljoyn/BusObject.h>
#include <alljoyn/DBusStd.h>
#include <alljoyn/Init.h>
#include <alljoyn/AboutData.h>
#include <alljoyn/AboutObj.h>
#include <alljoyn/MsgArg.h>
#include <alljoyn/Status.h>
#include <alljoyn/version.h>

#include "GPIOClass.h"

using namespace std;
using namespace qcc;
using namespace ajn;
using namespace ajn::services;

static const uint8_t appId[] = { 
    0x0F, 0xB4, 0xBA, 0x14,
    0x1E, 0x82, 0x11, 0xE4,
    0x86, 0x51, 0xD1, 0x56,
    0x1D, 0x5D, 0x46, 0xB0 
};
/*
class AboutStore : public PropertyStore {
  public:
    QStatus ReadAll(const char* languageTag, PropertyStore::Filter filter, MsgArg& all) {
        if (languageTag && strcmp(languageTag, "en") != 0) {
            return ER_LANGUAGE_NOT_SUPPORTED;
        }
        if (PropertyStore::WRITE == filter) {
            return ER_NOT_IMPLEMENTED;
        }

        size_t numProps = (PropertyStore::READ == filter) ? 11 : 7;
        MsgArg* props = new MsgArg[numProps];
        props[0].Set("{sv}", "AppId", new MsgArg("ay", 16, appId));
        props[1].Set("{sv}", "DefaultLanguage", new MsgArg("s", "en"));
        props[2].Set("{sv}", "DeviceName", new MsgArg("s", "hack buzzer-audio"));
        props[3].Set("{sv}", "DeviceId", new MsgArg("s", "a4c06771-c725-48c2-b1ff-6a2a59d445b8"));
        props[4].Set("{sv}", "AppName", new MsgArg("s", "Hackweek Buzzer Audio"));
        props[5].Set("{sv}", "Manufacturer", new MsgArg("s", "Infragistics"));
        props[6].Set("{sv}", "ModelNumber", new MsgArg("s", "1"));
        if (PropertyStore::READ == filter) {
            static const char* supportedLanguages[] = { "en" };
            props[7].Set("{sv}", "SupportedLanguages", new MsgArg("as", 1, supportedLanguages));
            props[8].Set("{sv}", "Description", new MsgArg("s", "Hackweek Buzzer Audio Sink"));
            props[9].Set("{sv}", "SoftwareVersion", new MsgArg("s", "v1.0.0"));
            props[10].Set("{sv}", "AJSoftwareVersion", new MsgArg("s", ajn::GetVersion()));
        }

        all.Set("a{sv}", numProps, props);
        all.SetOwnershipFlags(MsgArg::OwnsArgs, true);
        return ER_OK;
    }
};
*/

/** Static top level message bus object */
static BusAttachment* s_msgBus = NULL;

static SessionId s_sessionId = 0;

static const char* INTERFACE_NAME = "com.infragistics.HackBuzzer";
static const char* SERVICE_NAME = "com.infragistics.HackBuzzer";
static const char* SERVICE_PATH = "/buzzer";
static const SessionPort SERVICE_PORT = 25;

static volatile sig_atomic_t s_interrupt = false;

static void CDECL_CALL SigIntHandler(int sig)
{
    QCC_UNUSED(sig);
    s_interrupt = true;
}

static const char* objId = "obj";
static const char* ifcId = "ifc";
static const char* nameChangedId = "bellRing";
static bool opened = false;
static GPIOClass* gpio4;
static GPIOClass* gpio17;

class BuzzerObject : public BusObject {
  public:
    BuzzerObject(BusAttachment& bus, const char* path) :
        BusObject(path),
        bellRingMember(NULL)
    {
        SetDescription("", objId);

        InterfaceDescription* iface = NULL;
        QStatus status = bus.CreateInterface(INTERFACE_NAME, iface);
        if (status == ER_OK) {
            iface->SetDescriptionLanguage("");
            iface->SetDescription(ifcId);
            iface->AddSignal("bellRing", NULL, NULL, 0);
            iface->SetMemberDescription("bellRing", nameChangedId);
            iface->AddMethod("OpenDoor", NULL, "s", "result", 0, 0);
            iface->SetMemberDescription("OpenDoor", "OpenDoor");
            iface->AddMethod("RingBell", NULL, "s", "result", 0, 0);
            iface->SetMemberDescription("RingBell", "RingBell");
            iface->Activate();
        } else {
            printf("Failed to create interface %s\n", INTERFACE_NAME);
        }

        status = AddInterface(*iface, ANNOUNCED);
        if (status == ER_OK) {
            bellRingMember = iface->GetMember("bellRing");
            assert(bellRingMember);
        } else {
            printf("Failed to Add interface: %s", INTERFACE_NAME);
        }

        const MethodEntry methodEntries[] = {
            { iface->GetMember("OpenDoor"), static_cast<MessageReceiver::MethodHandler>(&BuzzerObject::OpenDoor) },
            { iface->GetMember("RingBell"), static_cast<MessageReceiver::MethodHandler>(&BuzzerObject::RingBell) }
        };
        AddMethodHandlers(methodEntries, sizeof(methodEntries) / sizeof(methodEntries[0]));
    }

    QStatus EmitBellRingSignal()
    {
        printf("Emiting Name Changed Signal.\n");
        assert(bellRingMember);
        if (0 == s_sessionId) {
            printf("Sending BellRing signal without a session id\n");
        }

        uint8_t flags = ALLJOYN_FLAG_GLOBAL_BROADCAST;
        QStatus status = Signal(NULL, s_sessionId, *bellRingMember, NULL, 5000, 0, flags);
        status = Signal(NULL, 0, *bellRingMember, NULL, 5000, 0, ALLJOYN_FLAG_GLOBAL_BROADCAST || ALLJOYN_FLAG_SESSIONLESS);
        return status;
    }

    void OpenDoor(const InterfaceDescription::Member* member, Message& msg) {
        if (opened) {   
            gpio17->setval_gpio("0");
            MsgArg outArg("s", "Closed");
            QStatus status = MethodReply(msg, &outArg, 1);
            if (ER_OK != status) {
                printf("Failed to create method reply.");
            }
        } else {
            gpio17->setval_gpio("1");
            MsgArg outArg("s", "Opened");
            QStatus status = MethodReply(msg, &outArg, 1);
            if (ER_OK != status) {
                printf("Failed to create method reply.");
            }
        }
        opened = !opened;
    }

    void RingBell(const InterfaceDescription::Member* member, Message& msg) {
        EmitBellRingSignal();   
    	MsgArg outArg("s", "Ringed!");
    	QStatus status = MethodReply(msg, &outArg, 1);
        if (ER_OK != status) {
            printf("Failed to create method reply.");
        }
    }

  private:
    const InterfaceDescription::Member* bellRingMember;
};

class BuzzerBusListener : public BusListener, public SessionPortListener {
    void NameOwnerChanged(const char* busName, const char* previousOwner, const char* newOwner)
    {
        if (newOwner && (0 == strcmp(busName, SERVICE_NAME))) {
            printf("NameOwnerChanged: name=%s, oldOwner=%s, newOwner=%s\n",
                   busName,
                   previousOwner ? previousOwner : "<none>",
                   newOwner ? newOwner : "<none>");
        }
    }

    bool AcceptSessionJoiner(SessionPort sessionPort, const char* joiner, const SessionOpts& opts)
    {
        if (sessionPort != SERVICE_PORT) {
            printf("Rejecting join attempt on unexpected session port %d\n", sessionPort);
            return false;
        }
        printf("Accepting join session request from %s (opts.proximity=%x, opts.traffic=%x, opts.transports=%x)\n",
               joiner, opts.proximity, opts.traffic, opts.transports);
        return true;
    }

    void SessionJoined(SessionPort sessionPort, SessionId id, const char* joiner) {
        s_sessionId = id;
    }
};

static BuzzerBusListener s_busListener;

/** Request the service name, report the result to stdout, and return the status code. */
QStatus RequestName(void)
{
    const uint32_t flags = DBUS_NAME_FLAG_REPLACE_EXISTING | DBUS_NAME_FLAG_DO_NOT_QUEUE;
    QStatus status = s_msgBus->RequestName(SERVICE_NAME, flags);

    if (ER_OK == status) {
        printf("RequestName('%s') succeeded.\n", SERVICE_NAME);
    } else {
        printf("RequestName('%s') failed (status=%s).\n", SERVICE_NAME, QCC_StatusText(status));
    }

    return status;
}

/** Create the session, report the result to stdout, and return the status code. */
QStatus CreateSession(TransportMask mask)
{
    SessionOpts opts(SessionOpts::TRAFFIC_MESSAGES, false, SessionOpts::PROXIMITY_ANY, mask);
    SessionPort sp = SERVICE_PORT;
    QStatus status = s_msgBus->BindSessionPort(sp, opts, s_busListener);

    if (ER_OK == status) {
        printf("BindSessionPort succeeded.\n");
    } else {
        printf("BindSessionPort failed (%s).\n", QCC_StatusText(status));
    }

    return status;
}

/** Advertise the service name, report the result to stdout, and return the status code. */
QStatus AdvertiseName(TransportMask mask)
{
    QStatus status = s_msgBus->AdvertiseName(SERVICE_NAME, mask);

    if (ER_OK == status) {
        printf("Advertisement of the service name '%s' succeeded.\n", SERVICE_NAME);
    } else {
        printf("Failed to advertise name '%s' (%s).\n", SERVICE_NAME, QCC_StatusText(status));
    }

    return status;
}

static BuzzerObject* buzzerObj = NULL;

void WatchGPIO(void)
{

    string inputstate;
    while (s_interrupt == false) {
        gpio4->getval_gpio(inputstate);
        if (inputstate == "0") {
	    if (buzzerObj) {
		buzzerObj->EmitBellRingSignal();
	    }
        }
#ifdef _WIN32
        Sleep(10);
#else
        usleep(10 * 1000);
#endif
    }
}

static void sigtime(int signo) 
{
    signal(SIGALRM, sigtime);
    if (buzzerObj) {
        buzzerObj->EmitBellRingSignal();
    }
}
/*
static int usage(const char* name) {
    printf("Usage: %s [-Ddevice] [-Mmixer]\n", name);
    return 1;
}
*/
/** Main entry point */
int CDECL_CALL main(int argc, char** argv, char** envArg)
{
    QCC_UNUSED(argc);
    QCC_UNUSED(argv);
    QCC_UNUSED(envArg);
/*
    const char* deviceName = "default";
    const char* mixerName = "default";
    for (int i = 1; i < argc; ++i) {
        if (!strncmp(argv[i], "-h", 2) || !strncmp(argv[i], "--h", 3)) {
            return usage(argv[0]);
        } else if (!strncmp(argv[i], "-D", 2)) {
            deviceName = &argv[i][2];
        } else if (!strncmp(argv[i], "-M", 2)) {
            mixerName = &argv[i][2];
        } else {
            return usage(argv[0]);
        }
    }

    setenv("PULSE_INTERNAL", "0", 1);
*/
    if (AllJoynInit() != ER_OK) {
        return 1;
    }
#ifdef ROUTER
    if (AllJoynRouterInit() != ER_OK) {
        AllJoynShutdown();
        return 1;
    }
#endif
/*
    AudioDevice* audioDevice = NULL;
    AboutStore* aboutProps = NULL;
    StreamObject* streamObj = NULL;
*/
    printf("AllJoyn Library version: %s.\n", ajn::GetVersion());
    printf("AllJoyn Library build info: %s.\n", ajn::GetBuildInfo());

    /* Install SIGINT handler */
    signal(SIGINT, SigIntHandler);

    gpio4 = new GPIOClass("4");
    gpio4->export_gpio();
    gpio4->setdir_gpio("in");

    gpio17 = new GPIOClass("17");
    gpio17->export_gpio();
    gpio17->setdir_gpio("out");

    QStatus status = ER_OK;

    /* Create message bus */
    s_msgBus = new BusAttachment("hackweek-buzzer", true);

    /* This test for NULL is only required if new() behavior is to return NULL
     * instead of throwing an exception upon an out of memory failure.
     */
    if (s_msgBus) {
        /* Register a bus listener */
        if (ER_OK == status) {
            s_msgBus->RegisterBusListener(s_busListener);
        }

        if (ER_OK == status) {
	    status = s_msgBus->Start();
	    if (ER_OK == status) {
		printf("BusAttachment started.\n");
	    } else {
		printf("Start of BusAttachment failed (%s).\n", QCC_StatusText(status));
	    }
        }

	BuzzerObject* obj = NULL;
        obj = new BuzzerObject(*s_msgBus, SERVICE_PATH);

        if (ER_OK == status) {
	    printf("Registering the bus object.\n");
	    s_msgBus->RegisterBusObject(*obj);

	    QStatus status = s_msgBus->Connect();

	    if (ER_OK == status) {
		printf("Connected to '%s'.\n", s_msgBus->GetConnectSpec().c_str());
	    } else {
		printf("Failed to connect to '%s'.\n", s_msgBus->GetConnectSpec().c_str());
	    }
        }

        /*
         * Advertise this service on the bus.
         * There are three steps to advertising this service on the bus.
         * 1) Request a well-known name that will be used by the client to discover
         *    this service.
         * 2) Create a session.
         * 3) Advertise the well-known name.
         */
        if (ER_OK == status) {
            status = RequestName();
        }

        const TransportMask SERVICE_TRANSPORT_TYPE = TRANSPORT_ANY;

        if (ER_OK == status) {
	    SessionOpts opts(SessionOpts::TRAFFIC_MESSAGES, false, SessionOpts::PROXIMITY_ANY, SERVICE_TRANSPORT_TYPE);
	    SessionPort sp = SERVICE_PORT;
	    status = s_msgBus->BindSessionPort(sp, opts, s_busListener);

	    if (ER_OK == status) {
		printf("BindSessionPort succeeded.\n");
	    } else {
		printf("BindSessionPort failed (%s).\n", QCC_StatusText(status));
	    }
        }

        if (ER_OK == status) {
            status = AdvertiseName(SERVICE_TRANSPORT_TYPE);
        }

	AboutData aboutData("en");
        aboutData.SetAppId(appId, 16);
        aboutData.SetDeviceName("buzzer-1");
        aboutData.SetDeviceId("a4c06771-c725-48c2-b1ff-6a2a59d445b8");
        aboutData.SetAppName("Hackweek Buzzer");
        aboutData.SetManufacturer("IG-UY");
        aboutData.SetModelNumber("1.0");
        aboutData.SetDescription("A magic buzzing buzzer.");
        aboutData.SetDateOfManufacture("2015-09-30");
        aboutData.SetSoftwareVersion("1.0");
        aboutData.SetHardwareVersion("1.0");
        aboutData.SetSupportUrl("mailto:indigo@infragistics.com");
       
        AboutObj aboutObj(*s_msgBus);
        status = aboutObj.Announce(SERVICE_PORT, aboutData);
        if (ER_OK != status) {
            printf("AboutObj Announce failed (%s)\n", QCC_StatusText(status));            
        } else {
            printf("AboutObj Announce succeeded.\n");            
        }
/*
        aboutProps = new AboutStore();
        audioDevice = new ALSADevice(deviceName, mixerName);
        streamObj = new StreamObject(s_msgBus, "/Speaker/In", audioDevice, SERVICE_PORT, aboutProps);
        status = streamObj->Register(s_msgBus);
        if (status != ER_OK) {
            printf("Failed to register stream object (%s)\n", QCC_StatusText(status));
        }
*/

        if (false && ER_OK == status) {
            signal(SIGALRM, sigtime);
	    itimerval itm;
	    itm.it_interval.tv_sec= 5;
	    itm.it_interval.tv_usec = 0;
	    itm.it_value.tv_sec = 5;
	    itm.it_value.tv_usec = 0;
	    setitimer(ITIMER_REAL,&itm,0);
        }  

        /* Perform the service asynchronously until the user signals for an exit. */
        if (ER_OK == status) {
            buzzerObj = obj;
            WatchGPIO();
        }
    } else {
        status = ER_OUT_OF_MEMORY;
    }
/*
    if (streamObj != NULL) {
        streamObj->Unregister();
        delete streamObj;
        streamObj = NULL;
    }
    delete audioDevice;
    audioDevice = NULL;
    delete aboutProps;
    aboutProps = NULL;
*/
    gpio4->unexport_gpio();
    delete gpio4;
    gpio17->unexport_gpio();
    delete gpio17;

    delete s_msgBus;
    s_msgBus = NULL;
    delete buzzerObj;

    buzzerObj = NULL;

    printf("Signal service exiting with status 0x%04x (%s).\n", status, QCC_StatusText(status));

#ifdef ROUTER
    AllJoynRouterShutdown();
#endif
    AllJoynShutdown();
    return (int) status;
}
