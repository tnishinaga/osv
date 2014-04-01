#!/bin/sh
set -e

version=$(`dirname $0`/osv-version.sh)
platform="$1"

function usage()
{
	echo "Usage: scripts/release-image.sh s3|gce"
	exit 1
}

if [ $# -ne 1 ];then
	usage
fi

if [ $platform == "s3" ]; then
	url_base="s3://downloads.osv.io/qemu"
elif [ $platform == "gce" ]; then
	url_base="gs://osv"
else
	usage
fi

function upload()
{
	local image=$1
	local hypervisor=$2
        local format=$3
	local url=$url_base/osv-$version-$hypervisor.$format
	local download=""

	echo "---------------- Uploading image --------------------------------------"

	if [ $platform == "s3" ]; then
#		s3cmd put $image $url
#		s3cmd setacl $url --acl-public
		download="http://downloads.osv.io.s3-website-us-east-1.amazonaws.com/qemu/osv-$version-$hypervisor.$format"
	elif [ $platform == "gce" ]; then
		gsutil cp $image $url
		gsutil acl set public-read $url
		download="http://storage.googleapis.com/osv/osv-$version-$hypervisor.$format"
	fi

	echo "IMAGE    = $image"
	echo "URL      = $url"
	echo "Format   = $format"
	echo "Download = $download"

}

function build()
{
	echo "---------------- Building image --------------------------------------"
	make -j8 clean
	make -j8 all
}

### Build image
build

### Image for KVM
image=build/release/usr.img
hypervisor=kvm
format=qcow2
upload $image $hypervisor $format

### Image for VirtualBox
scripts/gen-vbox-ova.sh >/dev/null
image=build/release/osv.ova
hypervisor=vbox
format=ova
upload $image $hypervisor $format

### Image for Google Compute Engine
scripts/gen-gce-tarball.sh >/dev/null
image=build/release/osv.tar.gz
hypervisor=gce
format=tar.gz
upload $image $hypervisor $format

### Image for VMware Workstation
make osv.vmdk >/dev/null
scripts/gen-vmx.sh
cd build/release
zip osv-vmw.zip osv.vmx osv.vmdk >/dev/null
cd -
image=build/release/osv-vmw.zip
hypervisor=vmw
format=zip
upload $image $hypervisor $format

### Image for VMware ESXi
ovftool build/release/osv.vmx build/release/osv-esxi.ova >/dev/null
image=build/release/osv-esxi.ova
hypervisor=esx
format=ova
upload $image $hypervisor $format
