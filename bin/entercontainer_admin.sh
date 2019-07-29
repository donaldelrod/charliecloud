#!/bin/bash

# $1 is for specifying UID
#   e.g. if 0, ssh as root
# $2 is for specifying PID
#   if 0, ssh into machine instead of container

#echo "$SSH_ORIGINAL_COMMAND"
#a=$(echo "$SSH_ORIGINAL_COMMAND" | awk '{print$1}')
#b=$(echo "$SSH_ORIGINAL_COMMAND" | awk '{print$2}')
#echo "1: $a"
#echo "2: $b"
DEBUGLOG="/tmp/layercake.$UID.log"

debug() {
  echo "$1" >> $DEBUGLOG
}

debug "In the tasty cake: $$"

#this is to allow for debugging: specify the UID of container to enter
if [ -n "$1" > 0 ]; then
    USERID=$1
else
    USERID=$UID
fi

echo "finding container started by uid $1"

PID=""

# this block finds a proper PID for the program, or quits if it can't
if [ -n "$2" > 0 ] ; then
    # using passed process pid
    PID="$2"
elif [ -f "/tmp/$USERID" ]; then
    # File exists, so task is most likey running (might have to
    # change for nodes with multiple jobs running)
    PID=$(cat "/tmp/$USERID" | grep process_pid | awk -F: '{ print$2 }')
else
    # No UID file exists, and nothing was passed
    echo "no container running by user $USERID or in process $PID, exiting"
    # No job id found, for now I'm going to just block the ssh connection
    exit 
fi


# Is the job using a user namespace?
if [ !  $(readlink /proc/self/ns/user) = $(readlink /proc/$PID/ns/user) ]
then
    debug "Different user namespaces"
    NSOPTIONS="-U"
fi

# Is the job using a mount namespace?
if [ !  $(readlink /proc/self/ns/mnt) = $(readlink /proc/$PID/ns/mnt) ]
then
    debug "Different mount namespaces"
    NSOPTIONS="$NSOPTIONS -m"
fi

# Build the nsenter command line that will be used to join the container
NSENTER="nsenter -t $PID $NSOPTIONS --preserve-credentials"

if [ ! -z "$SSH_ORIGINAL_COMMAND" ]
then
# The user passed a command to SSH.  Exec it within the container.
debug "$SSH_ORIGINAL_COMMAND"
exec $NSENTER $SHELL -c "$SSH_ORIGINAL_COMMAND"
else
# The user did not pass a command to SSH.  Exec their shell within the container.
debug "No command passed.  Running $SHELL"
exec $NSENTER $SHELL -l
fi