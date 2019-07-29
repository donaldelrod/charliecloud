#define _GNU_SOURCE

#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>

#include <slurm/spank.h>

#include <fcntl.h>
#include "charliecloud.h"

SPANK_PLUGIN(ch-slurm, 1)
extern int errno;

static char *imagename = NULL;
static char** mountpoints = NULL;
static int num_mounts = 0;

/**
 * /etc/slurm/* for conf files
 * From /usr/include/slurm/spank.h 
 * SPANK plugin operations. SPANK plugin should have at least one of
 * these functions defined non-NULL.
 *
 *  Plug-in callbacks are completed at the following points in slurmd:
 *
 *  slurmd
 *        `-> slurmd_init()
 *        |
 *        `-> job_prolog()
 *        |
 *        | `-> slurmstepd
 *        |      `-> init ()
 *        |       -> process spank options
 *        |       -> init_post_opt ()
 *        |      + drop privileges (initgroups(), seteuid(), chdir())
 *        |      `-> user_init ()
 *        |      + for each task
 *        |      |       + fork ()
 *        |      |       |
 *        |      |       + reclaim privileges
 *        |      |       `-> task_init_privileged ()
 *        |      |       |
 *        |      |       + become_user ()
 *        |      |       `-> task_init ()
 *        |      |       |
 *        |      |       + execve ()
 *        |      |
 *        |      + reclaim privileges
 *        |      + for each task
 *        |      |     `-> task_post_fork ()
 *        |      |
 *        |      + for each task
 *        |      |     `-> task_post_fork ()
 *        |      |
 *        |      + for each task
 *        |      |       + wait ()
 *        |      |          `-> task_exit ()
 *        |      `-> exit ()
 *        |
 *        `---> job_epilog()
 *        |
 *        `-> slurmd_exit()
 *
 *   In srun only the init(), init_post_opt() and local_user_init(), and exit()
 *    callbacks are used.
 *
 *   In sbatch/salloc only the init(), init_post_opt(), and exit() callbacks
 *    are used.
 *
 *   In slurmd proper, only the slurmd_init(), slurmd_exit(), and
 *    job_prolog/epilog callbacks are used.
 *
 */

/**
 * Sets up the environment for the Charliecloud container
 * Changes the $HOME variable to the correct path (/home/$USER) unless --no-home was specified
 * 
 */
void fix_environment(spank_t sp, struct container *c)
{
   char *name, *new_value;
   char old_value[PATH_MAX];

   // $HOME: Set to /home/$USER unless --no-home specified.
   if (!c->private_home) {
      spank_getenv(sp, "USER", old_value, PATH_MAX);
      // FIXME: the next line should test the return of the previous one
      if (old_value == NULL) {
         WARNING("$USER not set; cannot rewrite $HOME");
      } else {
         T_ (1 <= asprintf(&new_value, "/home/%s", old_value));
         //Z_ (setenv("HOME", new_value, 1));
         // FIXME: remove above line
         spank_setenv(sp, "HOME", new_value, true);
      }
   }

   // $PATH: Append /bin if not already present.
   spank_getenv(sp, "PATH", old_value, PATH_MAX);
   // FIXME: the next line should test the return of the previous one
   if (old_value == NULL) {
      WARNING("$PATH not set");
   } else if (   strstr(old_value, "/bin") != old_value
              && !strstr(old_value, ":/bin")) {
      T_ (1 <= asprintf(&new_value, "%s:/bin", old_value));
      //Z_ (setenv("PATH", new_value, 1));
      // FIXME: remove above line
      spank_setenv(sp, "PATH", new_value, true);
      INFO("new $PATH: %s", new_value);
   }
}


/**
 * Hoping that this function will properly solve the
 * problem of mounting user specified folders into the
 * container images by recursively creating the path
 * that the mounts will be mounted to
 */
void create_path(char* full_path) {
  printf("trying to create dir %s\n", full_path);
   char command[50];
   sprintf(command, "id && mkdir -p %s", full_path);
   if (system(command) != 0) {
      printf("failed to create the directory %s\n", full_path);
   } else printf("successfully created the directory %s\n", full_path);

  // slurm_info("trying to create dir %s\n", full_path);
  // //base case, returns if done
  // if (path_exists(full_path)) {
  //   slurm_info("reached base case\n");
  //   return;
  // }
  // //if the parent dir exists, make this dir
  // else if (path_exists(dirname(full_path)))
  //   slurm_info("trying to create dir %s\n", dirname(full_path));
  //   Z_(mkdir(full_path, 0755));
  // //otherwise recursively call this function
  // else {
  //   slurm_info("trying to create dir %s\n", dirname(full_path));
  //   create_path(dirname(full_path));
  //   slurm_info("trying to create dir %s\n", full_path);
  //   Z_(mkdir(full_path, 0755));
  // }
}

/**
 * This function is a helper function that takes in a comma
 * separated list of mount points and returns them as a char**
 */
static void extract_paths(char* allpaths) {

  int ind = 0, len = 0, nc = 0;

  // I'm trying to do my best with proper memory allocation, so here
  // I'm counting the number of commas so I can properly malloc my char**
  for (int i = 0; i < strlen(allpaths); i++)
    if (allpaths[i] == ',')
      nc++;
  
  // the char** will have a size of (number of commas + 1) * sizeof(char*)
  mountpoints = malloc((nc + 1) * sizeof(char*));

  num_mounts = nc+1;

  for (int i = 0; i < strlen(allpaths); i++) {
    if (allpaths[i] == ',') {
      //creates a memory location for the individual mount strings
      mountpoints[ind] = malloc((len+1) * sizeof(char));
      memset(mountpoints[ind], 0, len+1);

      // copy the char array to the char sub-array
      strncpy(&mountpoints[ind][0], &allpaths[i-len], len);

      // reset temp variables 
      len = 0;
      ind++;
    } else len++;
  }

  //get the last mount point
  mountpoints[ind] = malloc(len+1);
  memset(mountpoints[ind], 0, len+1);

  // copy the char array to the char sub-array
  strncpy(&mountpoints[ind][0], &allpaths[strlen(allpaths)-len], len);
}

static void generate_ssh_file() {

}

static int _opt_ch_mount(int val, const char* optarg, int remote) {
  char* givenmountpoint;

  slurm_info("opt_ch_mount initializing");

  // slurm_info("Remote: %d", remote);
  // slurm_info("Context: %d", spank_context());
  if (mountpoints == NULL) {
    givenmountpoint = strdup(optarg);
    extract_paths(givenmountpoint);
  }
  //slurm_info("finished parsing passed mounts");
  // for (int i = 0; i < num_mounts; i++) {
  //   slurm_info("mountpoint: %s", mountpoints[i]);
  // }

  return 0;
}

static int _opt_ch_image(int val, const char *optarg, int remote){
  char *givenimagename;

  slurm_info("opt_ch_image being initialized");
  slurm_info("Remote: %d", remote);
  slurm_info("Context: %d", spank_context());

  if(imagename == NULL){
    givenimagename = strdup(optarg);  // FIXME: check for failure
    imagename = givenimagename;//basename(givenimagename);  // FIXME: check for corner cases: "", "/", "foo/", "/foo/", ...
  }

  if(strncmp(imagename, "", 1) == 0){
    slurm_error("Image name cannot be empty");
    return -1;
  }

  return 0;
}

/**
 * This function is called on the remote host just after the job step is initialized, and
 * before any of the other slurm plugins are processed. This is where we should extract all
 * of the options we want to write to temporary file for enabling unhacky sshin
 */
int slurm_spank_init(spank_t sp, int ac, char **av){
  struct spank_option opt_ch_image = {
    .name = "ch-image",
    .arginfo = "image",
    .usage = "Charliecloud container image to run job in",
    .has_arg = 1,
    .val = 0,
    .cb = (spank_opt_cb_f) _opt_ch_image, // FIXME: need to add sanity checking
  };

  struct spank_option opt_ch_mount = {
    .name = "ch-mount",
    .arginfo = "mountpoint",
    .usage = "Defines a folder to mount into the container",
    .has_arg = 1,
    .val = 0,
    .cb = (spank_opt_cb_f) _opt_ch_mount,
  };

  slurm_info("Plugin initializing");

  if (spank_context() != S_CTX_ALLOCATOR) {
    spank_option_register(sp, &opt_ch_image);
    spank_option_register(sp, &opt_ch_mount);
  }

  slurm_info("Number of options: %d", ac);

  return 0;
}

/**
 * This function should hook into the slurmd daemon and execute right after the process is forked and
 * right before the forked program loses its priviledged rights
 * 
 * I also need to chmod the container folder here so that we can create the directories in the container
 * before we lose priviledge, the then chmod it back
 */
int slurm_spank_task_init_privileged(spank_t sp, int ac, char** av) {

  //grab the context to see if the function is executing on the 
  //compute nodes or the master
  int context = spank_remote(sp);
  if (context != 1) {
    slurm_info("not executing in a remote context, exiting plugin...");
    return 0;
  }

  //define variables for rest of function
  uid_t user_id;
  u_int32_t slurm_job_id;
  pid_t process_pid;
  spank_err_t rc;
  char filename[20];
  char s[20];

  slurm_info("Gathering process info to allow ssh-ing into your container");

  //grab the necessary info for the ssh script to run
  if ((rc = spank_get_item(sp, S_JOB_UID, &user_id)) != ESPANK_SUCCESS) 
    slurm_info("error getting uid: %d", rc);              //get user id
  else
    slurm_info("user_id of container: %u", user_id);
  
  if ((rc = spank_get_item(sp, S_TASK_PID, &process_pid)) != ESPANK_SUCCESS)  
    slurm_info("error getting process_pid: %d", rc);         //get the task's pid
  else
    slurm_info("process_pid: %u", process_pid);

  if ((rc = spank_get_item(sp, S_JOB_ID, &slurm_job_id)) != ESPANK_SUCCESS)  
    slurm_info("error getting slurm_job_id: %d", rc);         //get the task's pid
  else
    slurm_info("slurm_job_id: %u", slurm_job_id);

  //print filename into char buffer and print for slurm log
  sprintf(filename, "/tmp/%u.%u", user_id, slurm_job_id);
  slurm_info("filename with PID for ssh script to run: %s", filename);

  //open the tmp file and write it the user_id
  //might remove this because the name of the file is already the user id...
  int fd = open(filename, O_CREAT | O_RDWR);
  memset(s, 20, 0);
  sprintf(s, "user_id:%u\n", user_id);
  int n = write(fd, s, strlen(s));

  //writing the process's PID to string then file
  memset(s, 20, 0);
  sprintf(s, "process_pid:%u\n", getpid());
  write(fd, s, strlen(s));
  
  //write the slurm job id to string then file
  memset(s, 20, 0);
  sprintf(s, "slurm_job_id:%u\n", slurm_job_id);
  write(fd, s, strlen(s));
  close(fd);

  slurm_info("pulling image from ceph...");
  char comm[100];
  //easy buffer overflow, make sure to disable stack smashing
  slurm_info("image to download: %s", imagename);
  sprintf(comm, "/bin/sh /software/charliecloud/0.9.8-slurm/bin/ceph-pull.sh %s %d", imagename, user_id);
  slurm_info("command running to download ceph image: %s", comm);
  if (system(comm) != 0)
    slurm_info("error downloading the ceph image %s", imagename);
  else slurm_info("successfully downloaded the image %s", imagename);

  imagename = cat("/containers/", imagename);
  char newimagename[100];
  sprintf(newimagename, "%s_%d\0", imagename, user_id);
  imagename = malloc(strlen(newimagename));
  strcpy(imagename, newimagename);
  
  slurm_info("done pulling image, creating mount infrastructure...");

  //slurm_info("in task_init_priviledged, the number of mounts to create dirs for is %d\n", num_mounts);

  for (int i = 0; i < num_mounts; i++) {
    slurm_info("creating directory %s in image location %s", mountpoints[i], imagename);
    char* dst_full = cat(imagename, mountpoints[i]);
    create_path(dst_full);
  }

  return 0;
}

/**
 * This function is called on exit, it is called so it can clean up
 * the temp file created in the task_init_privileged spank function
 */
int slurm_spank_exit(spank_t sp, int ac, char** av) {
  //function variables
  uid_t user_id;
  u_int32_t slurm_job_id;
  char filename[20];
  spank_err_t rc;

  //get the context of the function execution
  //if the function is not running on the compute nodes, exit
  int context = spank_remote(sp);
  if (context != 1) {
    slurm_info("not executing in a remote context, exiting plugin...");
    return 0;
  }

  //grab the user id
  if ((rc = spank_get_item(sp, S_JOB_UID, &user_id)) != ESPANK_SUCCESS) 
    slurm_info("error getting uid: %d", rc);
  else
    slurm_info("user_id: %u", user_id);

  if ((rc = spank_get_item(sp, S_JOB_ID, &slurm_job_id)) != ESPANK_SUCCESS)  
    slurm_info("error getting slurm_job_id: %d", rc);         //get the task's pid
  else
    slurm_info("slurm_job_id: %u", slurm_job_id);
  
  sprintf(filename, "/tmp/%u.%u", user_id, slurm_job_id);

  if (remove( filename ) != 0)
    slurm_info("Error cleaning up");
  else
    slurm_info("Cleaned up ssh file successfully");

  char rmcontainer[60];
  sprintf(rmcontainer, "rm -rf %s", imagename);
  if (system(rmcontainer) == 0)
    slurm_info("successfully removed %s", imagename);
  else slurm_info("failed to remove %s", imagename);
  
  return 0;
}


int slurm_spank_task_init(spank_t sp, int ac, char **av){
  uid_t euid;
  gid_t egid;
  struct container c;
  char old_home[PATH_MAX];

  slurm_info("in slurm_spank_task_init");

  if(imagename != NULL) {
    slurm_info("Number of options: %d", ac);
    slurm_info("Image name: %s", imagename);

    euid = geteuid();
    egid = getegid();
    slurm_info("euid: %d", euid);

    prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);

    c.binds = calloc(1 + num_mounts, sizeof(struct bind));
    c.container_gid = egid;
    c.container_uid = euid;
    c.join = false;
    c.join_ct = 0;
    c.join_pid = 0;
    c.join_tag = NULL;
    c.private_home = false;
    c.private_tmp = false;
    spank_getenv(sp, "HOME", old_home, PATH_MAX);
    c.old_home = old_home;
    c.writable = false;
    c.sp = sp;

    slurm_info("Old home: %s", c.old_home);

    //c.newroot = "/images/mpihello";

    //asprintf(&c.newroot, "/images/%s", imagename);  // FIXME: check for failure
    asprintf(&c.newroot, "%s", imagename);  // FIXME: check for failure
    slurm_info("container name: %s", c.newroot);
    fix_environment(sp, &c);

    for (int i = 0; i < num_mounts; i++) {
      slurm_info("mountpoint: %s", mountpoints[i]);
    }

    //add passed mounts to the container struct
    if (num_mounts > 0) {
      for (int i = 0; i < num_mounts; i++) {
        //struct bind* b = malloc(sizeof(struct bind));
        c.binds[i].src = mountpoints[i];
        c.binds[i].dst = mountpoints[i];
        //c.binds[i] = *b;
      }
      c.binds[num_mounts].src = NULL;
      c.binds[num_mounts].dst = NULL;
    }

    containerize(&c);
  }
}
