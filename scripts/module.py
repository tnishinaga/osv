#!/usr/bin/python2

import json
import sys
import os
import subprocess
import hashlib

properties_version = 1

f = open("../../../config.json")
conf = json.load(f)
f.close()

mtype = sys.argv[1]
mskel = open("../../../%s.manifest.skel" % mtype)
m = open("../%s.manifest" % mtype, "w")
for l in mskel.readlines():
	m.write(l)
mskel.close()

if os.path.exists("tmp") == True:
	os.rmdir("tmp")

mprops = {}
for mod in conf["modules"]:
	md5 = hashlib.md5()
	md5.update("%s" % mod["path"])
	mprop_path = "%s.properties" % md5.hexdigest()
	if os.path.exists(mprop_path) == False:
		if mod["type"] == "git":
			cmd = "git clone -b %s %s tmp" % (mod["branch"], mod["path"])
		elif mod["type"] == "svn":
			cmd = "svn co %s tmp" % (mod["path"])
		elif mod["type"] == "dir":
			cmd = "cp -a %s tmp" % (mod["path"])
		else:
			raise Exception("%s is unknown type" % mod["type"])
		print cmd
		subprocess.call([cmd], shell=True)
		f = open("tmp/properties.json")
		mprop = json.load(f)
		f.close()
		os.rename("tmp", mprop["name"])
		os.symlink("%s/properties.json" % mprop["name"], mprop_path)
	else:
		f = open(mprop_path)
		mprop = json.load(f)
		f.close()
	mprops[mprop["name"]] = mprop

for mprop in mprops.values():
	mmod_path = "%s/%s.manifest" % (mprop["name"], mtype)
	if os.path.exists(mmod_path) == False:
		if mprop["properties_version"] != properties_version:
			raise Exception("[%s] unsupported properties version" % mprop["name"])
		for dep in mprop["dependencies"]:
			if mprop.has_key(dep) == False:
				raise Exception("[%s] requires %s" % (mprop["name"], dep))
		cmd = "make module"
		print cmd
		subprocess.call([cmd], cwd=mprop["name"], shell=True)
	print "append %s to %s.manifest" % (mmod_path, mtype)
	mmod = open(mmod_path)
	for l in mmod.readlines():
		m.write(l)
	mmod.close()
m.close()
