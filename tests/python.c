
#include "Python.h"

#include <stdlib.h>
#include <dlfcn.h>

int main(int argc, char **argv)
{
	setenv("PYTHONPATH", "/usr/lib/python2.7:/usr/lib/python2.7/dist-packages", 1);

	dlopen("/usr/lib/libpython2.7.so", 0);

	Py_SetProgramName(argv[0]);
	Py_Initialize();
	PySys_SetArgvEx(argc, argv, 0);

	PyRun_SimpleString("print 'Hello World'\n");
}
