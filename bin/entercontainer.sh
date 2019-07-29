#!/bin/bash

DEBUGLOG=/tmp/layercake.log

debug() {
  echo "$1" >> $DEBUGLOG
}

debug "In the tasty cake: $$"

#this is to allow for debugging: specify the UID of container to enter
USERID=$UID
PID=""
JOBS_PRESENT=0
# See if we got a slurm job ID for this node

# this is a test for multi-job nodes, check if any jobs exist
for i in /tmp/$UID.*; do test -f "$i" && JOBS_PRESENT=1 && break; done

if [ $JOBS_PRESENT ]; then #[ -f "/tmp/$USERID" ]; then
    # File exists, so task is most likey running (might have to
    # change for nodes with multiple jobs running)

    JOB_IDS=$(ls /tmp/$UID.* | awk -F. '{print$2}' 2>/dev/null)
    NUM_JOBS=$(echo ${JOB_IDS[*]} | wc -w)

    # if there is only one job, don't prompt user
    if [ $NUM_JOBS -eq 1 ]; then
        JOB_NO=$JOB_IDS
    # otherwise, prompt the user for which job to enter
    else
        VALID_JOB=0
        echo "There are $NUM_JOBS jobs running, please enter the job number of the environment you want to enter:"
        # PS3=">"
        # select opt in "${JOB_IDS[@]}"; do 
        #     [[ -n $opt ]] && JOB_NO=${JOB_IDS[]} && break;
        #     # case $opt in
        #     #     ${JOB_IDS[@]} ) 
        #     #         JOB_NO=${JOB_IDS[@]}
        #     #         break;
        #     #         ;;
        #     # esac
        # done

        echo "$JOB_IDS"
        while [ $VALID_JOB -eq 0 ]; do
            
            read -p ">" JOB_NO

            if [[ $JOB_IDS =~ $JOB_NO ]]; then
                VALID_JOB=1
            else
                echo "You did not select a valid job, please enter a currently executing job number"
            fi
        done
        echo "Entering job number $JOB_NO"
        
    fi

    PID=$(cat "/tmp/$USERID.$JOB_NO" | grep process_pid | awk -F: '{ print$2 }')

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

else
    echo "no container running, exiting..."
    # No job id found, for now I'm going to just block the ssh connection
    exit 
fi

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
