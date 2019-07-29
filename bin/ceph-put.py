import rados, sys
#arg1: pool
#arg2: object name

cluster = rados.Rados(conffile='/etc/ceph/ceph.conf', conf=dict(keyring='/etc/ceph/ceph.client.admin.keyring'))

cluster.connect()

ioctx = cluster.open_ioctx(sys.argv[1])

ioctx.write_full(sys.argv[2],sys.argv[2])

ioctx.close()
cluster.shutdown()
