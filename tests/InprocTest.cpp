#include "UiTest.h"

#include <decode/core/Rc.h>
#include <decode/groundcontrol/Exchange.h>
#include <decode/groundcontrol/FwtState.h>
#include <decode/groundcontrol/Atoms.h>
#include <decode/groundcontrol/AllowUnsafeMessageType.h>

#include "photon/core/Core.Component.h"
#include "photon/exc/Exc.Component.h"

#include <bmcl/Logging.h>
#include <bmcl/SharedBytes.h>

#include <tclap/CmdLine.h>

#include <caf/send.hpp>

using namespace decode;

DECODE_ALLOW_UNSAFE_MESSAGE_TYPE(bmcl::SharedBytes);

class PhotonStream : public caf::event_based_actor {
public:
    PhotonStream(caf::actor_config& cfg)
        : caf::event_based_actor(cfg)
    {
        Photon_Init();
        _current = *PhotonExc_GetMsg();
    }

    caf::behavior make_behavior() override
    {
        return caf::behavior{
            [this](SendDataAtom, const bmcl::SharedBytes& data) {
                PhotonExc_AcceptInput(data.data(), data.size());
                Photon_Tick();
            },
            [this](SetStreamDestAtom, const caf::actor& actor) {
                _dest = actor;
            },
            [this](StartAtom) {
                send(this, RepeatStreamAtom::value);
            },
            [this](RepeatStreamAtom) {
                Photon_Tick();
                auto data = bmcl::SharedBytes::create(_current.data, _current.size);
                send(_dest, RecvDataAtom::value, data);
                PhotonExc_PrepareNextMsg();
                _current = *PhotonExc_GetMsg();
                delayed_send(this, std::chrono::milliseconds(10), RepeatStreamAtom::value);
            },
        };
    }

    void on_exit() override
    {
        destroy(_dest);
    }

    PhotonExcMsg _current;
    caf::actor _dest;
};

using namespace decode;

int main(int argc, char** argv)
{
    TCLAP::CmdLine cmdLine("SerialTest");
    TCLAP::ValueArg<uint64_t> srcArg("m", "mcc-id", "Mcc id", false, 1, "number");
    TCLAP::ValueArg<uint64_t> destArg("u", "uav-id", "Uav id", false, 2, "number");

    cmdLine.add(&srcArg);
    cmdLine.add(&destArg);
    cmdLine.parse(argc, argv);

    return runUiTest<PhotonStream>(argc, argv, srcArg.getValue(), destArg.getValue());
}

