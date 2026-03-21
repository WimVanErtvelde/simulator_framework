/** <pre>
 *  This Class will process any unassigned packets
 *
 *  FILENAME:   DefaultProc.cpp
 *  LANGUAGE:   C++
 *  CLASS:      UNCLASSIFIED
 *  PROJECT:    CIGI
 *
 *  PROGRAM DESCRIPTION:
 *  ...
 *
 *  MODIFICATION NOTES:
 *  DATE     NAME                                SCR NUMBER
 *  DESCRIPTION OF CHANGE........................
 *
 *  04/29/2005 Greg Basler                       INIT
 *  Initial Release.
 *
 * </pre>
 *  Author: The Boeing Company
 *  Version: 0.1
 */


#include "DefaultProc.h"

#include <CigiDefaultPacket.h>

using namespace CIGI_IG_Interface_NS;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

// ================================================
// DefaultProc
// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
DefaultProc::DefaultProc()= default;

// ================================================
// ~DefaultProc
// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
DefaultProc::~DefaultProc()= default;


// ================================================
// OnPacketReceived
// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
void DefaultProc::OnPacketReceived(CigiBasePacket* packet)
{
}
