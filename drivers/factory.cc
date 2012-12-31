#include "drivers/factory.hh"
#include "drivers/driver.hh"
#include "debug.hh"

Factory* Factory::pinstance = 0;

void
Factory::AddDevice(u8 bus, u8 slot, u8 func) {
    // read device id and vendor id
    u32 ids = read_pci_config(bus, slot, func, 0);
    u16 dev_id = ids >> 16;
    u16 vid = ids & 0xff;
    Driver* dev = new Driver(dev_id, vid);
   // _drivers.insert(dev);
    dev->dumpConfig();
}

void
Factory::DumpDevices() {
    //for (auto ii = _drivers.begin() ; ii != _drivers.end() ; ii++ )
       //  (*ii)->dumpConfig();
}
