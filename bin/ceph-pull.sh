#!/bin/bash
scriptPath="/software/charliecloud/0.9.8-slurm/bin"
extension=".sqfs"
cf="/containers"
logpath="/tmp/ceph_rados_log_file.logfile"

echo "running the .py as $(whoami)..." > $logpath
python2 "$scriptPath/ceph-pull.py" containers "$1$extension" && echo "successfully executed python script, now unsquashing..." >> $logpath
unsquashfs -d "$cf/$1_$2" "$cf/$1$extension" && echo "successfully unsquashed image, now moving image dir to $cf ..." >> $logpath
#mv "squashfs-root" "$cf/$1" && echo "successfully moved the image from squashfs-root to $1" >> $logpath
#chmod a+rwx "$cf/$1" --recursive && echo "successfully chmodded the image" >> $logpath

#chown causes te container to exit incorrectly which makes the mounts hang

#chown "$2:$2" "$cf/$1" && echo "successfully changed ownership of container to $2"
#chmod +rw "$cf/$1_$2"
#echo "the permissions of the chmodded image dir is:" >> $logpath
ls -l "$cf/$1_$2" >> $logpath
echo done. >> $logpath
