
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

.PHONEY: all gcc

all: gcc

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

