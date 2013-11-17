#!/usr/bin/python2

import json
import sys
import os
import subprocess

repo_url = "https://github.com/syuu1228/osv-module-repository.git"
repo_path = "build/repository"

def usage():
	print "Usage:"
	print "%s update" % sys.argv[0]
	print "%s search [name]" % sys.argv[0]

def search_module(name):
	f = open("%s/modules.json" % repo_path)
	modules = json.load(f)
	f.close()
	for mod in modules:
		if name in mod["name"]:
			return mod
	return None

if len(sys.argv) < 2:
	usage()
	sys.exit()

action = sys.argv[1]
if action == "update":
	if os.path.exists(repo_path) == False:
		cmd = "git clone %s %s" % (repo_url, repo_path)
		subprocess.call([cmd], shell=True)
	else:
		subprocess.call(["git pull"], cwd=repo_path, shell=True)
elif action == "search":
	if os.path.exists(repo_path) == False:
		print "Do update fist" % sys.argv[0]
		sys.exit()
	if len(sys.argv) < 3:
		usage()
		sys.exit()
	name = sys.argv[2]
	mod = search_module(name)
	if mod != None:
		print json.dumps(mod)
	else:
		print "No module exists: %s" % name
elif action == "list":
	if os.path.exists(repo_path) == False:
		print "Do update first" % sys.argv[0]
		sys.exit()
	f = open("%s/modules.json" % repo_path)
	modules = json.load(f)
	f.close()
	for mod in modules:
		print mod["name"]
