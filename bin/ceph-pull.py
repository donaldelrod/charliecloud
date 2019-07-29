import rados, sys, os.path

cluster = rados.Rados(conffile='/etc/ceph/ceph.conf', conf=dict(keyring='/etc/ceph/ceph.client.admin.keyring'))

cluster.connect()

ioctx = cluster.open_ioctx(sys.argv[1])

#ioctx.remove_object(sys.argv[1])
#f=open(sys.argv[2],"w+b")
#f.write(bytes(ioctx.read(sys.argv[2])))

#.stat() returns a tuple whose first argument is the filesize
size=int(ioctx.stat(sys.argv[2])[0])
completed=int(0)
f=open(os.path.join('/containers/',sys.argv[2]),"wb")
while completed < size:
	if (completed +32768) < size:
		f.write(ioctx.read(sys.argv[2],32768,completed))
		completed = completed + 32768
	else:
		f.write(ioctx.read(sys.argv[2],(size - completed),completed))
		completed = size
f.close()
ioctx.close()
cluster.shutdown()
