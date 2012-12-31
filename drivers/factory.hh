#ifndef FACTORY_H
#define FACTORY_H

#include "drivers/pci.hh"
#include "drivers/driver.hh"
#include <unordered_set>

using namespace pci;

class Factory {
public:
    static Factory* Instance() {return (pinstance)? pinstance: (pinstance = new Factory);};
    void AddDevice(u8 bus, u8 slot, u8 func);

    void DumpDevices();

private:
   Factory() {pinstance = 0;};
   Factory(const Factory& f) {};
   Factory& operator=(const Factory& f) {pinstance = f.pinstance; return *pinstance;};

   static Factory* pinstance;
   std::unordered_set<const Driver*, Driver::hash, Driver::equal> _drivers;
};

#endif
