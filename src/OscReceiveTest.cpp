/*
	oscpack -- Open Sound Control (OSC) packet manipulation library
    http://www.rossbencina.com/code/oscpack

    Copyright (c) 2004-2013 Ross Bencina <rossb@audiomulch.com>

	Permission is hereby granted, free of charge, to any person obtaining
	a copy of this software and associated documentation files
	(the "Software"), to deal in the Software without restriction,
	including without limitation the rights to use, copy, modify, merge,
	publish, distribute, sublicense, and/or sell copies of the Software,
	and to permit persons to whom the Software is furnished to do so,
	subject to the following conditions:

	The above copyright notice and this permission notice shall be
	included in all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
	ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
	CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
	WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

/*
	The text above constitutes the entire oscpack license; however, 
	the oscpack developer(s) also make the following non-binding requests:

	Any person wishing to distribute modifications to the Software is
	requested to send the modifications to the original developer so that
	they can be incorporated into the canonical version. It is also 
	requested that these non-binding requests be included whenever the
	above license is reproduced.
*/
#include "OscReceiveTest.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <QThread>
//#include <threads>
#include <mutex>


#define oscClientAddress "192.168.0.125"
#define oscPortOut 9000
#define oscPortIn 9001
#define OUTPUT_BUFFER_SIZE 1024
#define IP_MTU_SIZE 1536

////


#include "osc/OscReceivedElements.h"

#include "ip/UdpSocket.h"
#include "osc/OscPacketListener.h"

#include "control/controlobject.h"
#include "control/controlproxy.h"
///

#include "controllers/scripting/legacy/controllerscriptinterfacelegacy.h"

///
//float a3;

namespace osc{

class OscReceiveTestPacketListener : public OscPacketListener{
  protected:

    void ProcessMessage( const osc::ReceivedMessage& m, 
            const IpEndpointName& remoteEndpoint )
    {
        (void) remoteEndpoint; // suppress unused parameter warning
        try {
            ReceivedMessageArgumentStream args = m.ArgumentStream();
            ReceivedMessage::const_iterator arg = m.ArgumentsBegin();

//                bool a1;
//                osc::int32 a2;
                float a3;
//                  float oscValue;
//                const char *a4;
//                args >> a1 >> a2 >> a3 >> a4 >> osc::EndMessage;
//                args >> oscValue >> osc::EndMessage;
                args >> a3 >> osc::EndMessage;

                oscResult oscIn;

                oscIn.oscAddress = m.AddressPattern();
                oscIn.oscGroup, oscIn.oscKey;
                oscIn.oscAddress.replace("/", "");
                oscIn.oscValue = a3;
                int posDel = oscIn.oscAddress.indexOf("@", 0, Qt::CaseInsensitive); 
                if (posDel > 0) {
                    oscIn.oscGroup = oscIn.oscAddress.mid(0, posDel);
                    oscIn.oscKey = oscIn.oscAddress.mid(posDel + 1, oscIn.oscAddress.length());

                    QString MixxxOSCStatusFileLocation = "/MixxxOSCStatus.txt";
                    QFile MixxxOSCStatusFile(MixxxOSCStatusFileLocation);
                    MixxxOSCStatusFile.open(QIODevice::ReadWrite | QIODevice::Append);
                    QTextStream MixxxOSCStatusTxt(&MixxxOSCStatusFile);
                    MixxxOSCStatusTxt << QString(" received message @ GROUP: %1, KEY: %2 and VALUE : %3").arg(oscIn.oscGroup).arg(oscIn.oscKey).arg(oscIn.oscValue) << "\n";
                    MixxxOSCStatusFile.close();

                    ControlObject::getControl(oscIn.oscGroup, oscIn.oscKey)->set(oscIn.oscValue);
                }


        }catch( Exception& e ){
            std::cout << "error while parsing message: "
                        << m.AddressPattern() << ": " << e.what() << "\n";
        }
    }    
};


void RunReceiveTest(int oscportin) {
    osc::OscReceiveTestPacketListener listener;
	UdpListeningReceiveSocket s(
            IpEndpointName( IpEndpointName::ANY_ADDRESS, oscportin ),
            &listener );

    QString MixxxOSCStatusFileLocation = "/MixxxOSCStatus.txt";
    QFile MixxxOSCStatusFile(MixxxOSCStatusFileLocation);
    //    MixxxOSCStatusFile.remove();
    MixxxOSCStatusFile.open(QIODevice::ReadWrite | QIODevice::Append);
    QTextStream MixxxOSCStatusTxt(&MixxxOSCStatusFile);
    MixxxOSCStatusTxt << QString(" listening on port : %1").arg(oscportin) << "\n";
    MixxxOSCStatusFile.close();

    s.Run();
}

} // namespace osc

#ifndef NO_OSC_TEST_MAIN

int EveOscIn()
{
    QString MixxxOSCStatusFileLocation = "/MixxxOSCStatus.txt";
    QFile MixxxOSCStatusFile(MixxxOSCStatusFileLocation);
    MixxxOSCStatusFile.remove();
	int oscportin = 9001;

    std::thread tosc(osc::RunReceiveTest, oscportin);
    tosc.detach();
    return 0;
}

#endif /* NO_OSC_TEST_MAIN */

