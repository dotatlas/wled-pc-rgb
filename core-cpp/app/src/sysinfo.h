// sysinfo — small platform probes. motherboard() reads the board make/model
// straight from the firmware's SMBIOS table (a genuinely low-level, no-driver
// read), which is the "detect your motherboard" piece of Goal #1.
#pragma once
#include <QString>

namespace sysinfo {
QString motherboard();   // e.g. "Micro-Star International Co., Ltd. PRO X870E-P WIFI (MS-7E70)"
}
