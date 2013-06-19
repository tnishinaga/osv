
# $(call git-clone dir url tag)
define git-clone =
test -d $1 || { git init $1 && cd $1 && git remote add origin $2; }
cd $1 && git fetch origin
cd $1 && git checkout $3
endef

# $(call svn-clone dir url tag)
define svn-clone =
svn co -q $2/$3 $1
endef

O=../build/external

.PHONEY: all gcc boost jdk

all: gcc boost jdk

gcc:
	mkdir -p $O
	$(call svn-clone,$O/gcc,svn://gcc.gnu.org/svn/gcc,tags/gcc_4_7_3_release)
	cd $O/gcc && ./configure \
		CFLAGS='-mno-red-zone -O2' \
		CXXFLAGS='-mno-red-zone -O2' \
		--disable-bootstrap \
		--with-multilib-list=m64 \
		--enable-shared=libgcc,libstdc++ \
		--enable-languages=c,c++ \
		--prefix=$(abspath $O/bin/usr)
	$(MAKE) -C $O/gcc
	$(MAKE) -C $O/gcc install
	ln -sf usr/lib64 $O/bin/lib64

boost:
	mkdir -p $O
	$(call svn-clone,$O/boost,http://svn.boost.org/svn/boost,tags/release/Boost_1_50_0)
	mkdir -p $O/bin/usr
	ln -sf lib64 $O/bin/usr/lib
	cd $O/boost && ./bootstrap.sh \
		--with-libraries=program_options \
		--prefix=$(abspath $O/bin/usr)
	cd $O/boost && ./b2 threading=multi cxxflags=-mno-red-zone
	rm -rf $O/bin/usr/lib64/libboost*
	cd $O/boost && ./b2 install
	for i in $O/bin/usr/lib64/libboost*.{a,so}; do mv $$i $$(echo $$i | sed -E 's/\.(a|so)$$/-mt.\1/'); done 

openjdk.bin = /usr/lib/jvm/java-1.7.0-openjdk.x86_64

# export BUILD_HEADLESS_ONLY=true

jdk-extra = EXTRA_CFLAGS=-mno-red-zone

jdk:
	mkdir -p $O
	test -d $O/jdk || hg clone http://icedtea.classpath.org/hg/release/icedtea7-2.3 $O/jdk
	cd $O/jdk && hg pull -u
	cd $O/jdk && ./autogen.sh
	cd $O/jdk && $(jdk-extra) ./configure \
		--disable-hg \
		--disable-docs \
		--disable-bootstrap \
		--enable-system-lcms \
		--enable-system-zlib \
		--enable-system-png \
		--enable-system-gif \
		--with-parallel-jobs \
		--disable-tests \
		--with-rhino
	sed -i 's/DISABLE_INTREE_EC="true"/DISABLE_INTREE_EC=""/' $O/jdk/Makefile
	cd $O/jdk && $(jdk-extra) make

