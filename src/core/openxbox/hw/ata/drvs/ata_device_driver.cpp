// ATA/ATAPI-4 emulation for the Original Xbox
// (C) Ivan "StrikerX3" Oliveira
//
// This code aims to implement a subset of the ATA/ATAPI-4 specification
// that satisifies the requirements of an IDE interface for the Original Xbox.
//
// Specification:
// http://www.t13.org/documents/UploadedDocuments/project/d1153r18-ATA-ATAPI-4.pdf
//
// References to particular items in the specification are denoted between brackets
// optionally followed by a quote from the specification.
#include "ata_device_driver.h"

#include "openxbox/log.h"
#include "openxbox/io.h"

namespace openxbox {
namespace hw {
namespace ata {

IATADeviceDriver::~IATADeviceDriver() {
}

}
}
}
