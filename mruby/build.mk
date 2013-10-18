mruby-objects = mruby/mruby.o mruby/libmruby.a
mirb-objects = mruby/mirb.o mruby/libmruby.a

define mruby-includes
  external/mruby/src
  external/mruby/include
endef

cflags-mruby-include = $(foreach path, $(strip $(mruby-includes)), -isystem $(src)/$(path))

$(mruby-objects): local-includes += $(cflags-mruby-include)
$(mruby-objects): post-includes-bsd =
$(mruby-objects): kernel-defines =
$(mruby-objects): CFLAGS += -Wno-unknown-pragmas

$(mirb-objects): local-includes += $(cflags-mruby-include)
$(mirb-objects): post-includes-bsd =
$(mirb-objects): kernel-defines =
$(mirb-objects): CFLAGS += -Wno-unknown-pragmas

mruby/libmruby.a:
	cp ../../mruby/build_config.rb ../../external/mruby
	make -C ../../external/mruby clean
	make -C ../../external/mruby
	mkdir -p mruby/
	cp ../../external/mruby/build/host/lib/libmruby.a mruby/

mruby/mruby.o:
	$(CC) $(CFLAGS) -c -o $@ ../../external/mruby/mrbgems/mruby-bin-mruby/tools/mruby/mruby.c

mruby/mirb.o:
	$(CC) $(CFLAGS) -c -o $@ ../../external/mruby/mrbgems/mruby-bin-mirb/tools/mirb/mirb.c

mruby.so: $(mruby-objects)
	$(makedir)
	$(q-build-so)

mirb.so: $(mirb-objects)
	$(makedir)
	$(q-build-so)
