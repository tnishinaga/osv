#include "drivers/driver.hh"
#include "debug.hh"

bool Driver::isPresent() {
    return false;
}

u16 Driver::getStatus() {
    return 0;
}

void Driver::setStatus(u16 s) {
}

void Driver::dumpConfig() const {
    debug(fmt("device: %d:%d") % (int)_id % (int)_vid);
}

std::ostream& operator << (std::ostream& out, const Driver& d) {
   out << "driver dev id=" << (int)d._id << " vid=" << (int)d._vid << std::endl;
   return out;
}
