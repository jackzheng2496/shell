#ifndef SFISH_H
#define SFISH_H

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>
#include <readline/readline.h>
#include <readline/history.h>

#define LEN(x) (sizeof(x) / sizeof(x[0]))
#define FILEPERMISSION O_WRONLY | O_TRUNC | O_CREAT
#define FILEMODE S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR
#define BOLD 8

typedef struct Process {
	char *process_name;
	char *process_name_copy;
	char **argv;
	int argc;
	int current_status;
	int fd_in, fd_out, fd_err, fds[2];
	int isBuiltin;
	pid_t pid;
	struct Process *next_process;

} Process_t;

typedef struct Job {
	Process_t *process_head;
	char *cmd;
	char *time_created;
	pid_t pgid;
	pid_t pid;
	int jid;
	int current_status;
	int exit_status;
	int bg;
	int interactive;
	struct Job *next_job;

} Job_t;

typedef enum {BLACK, RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE,
	      BBLACK, BRED, BGREEN, BYELLOW, BBLUE, BMAGENTA, BCYAN, BWHITE, RESET} COLORS;

typedef enum {NOTBUILTIN=-1, HELP, EXIT, CD, PWD, PRT, CHPMT, CHCLR, JOBS, FG, BG, KILL, DISOWN} BUILTIN;
/** color codes **/
const char *CC[] = {
	"\x1b[30m",
	"\x1b[31m",
	"\x1b[32m",
	"\x1b[33m",
	"\x1b[34m",
	"\x1b[35m",
	"\x1b[36m",
	"\x1b[37m",
	"\x1b[1;30m",
	"\x1b[1;31m",
	"\x1b[1;32m",
	"\x1b[1;33m",
	"\x1b[1;34m",
	"\x1b[1;35m",
	"\x1b[1;36m",
	"\x1b[1;37m",
	"\x1b[0m"
};

/** help statement builtin functions **/
const char *HELPMENU[] = {
	"help 				- Prints out this help menu\n",
	"exit 				- Exits sfish using the exit(3) function\n",
	"cd [PATH] 			- Changes the current working directory to PATH using the chdir(2) system call\n",
	"pwd 				- Prints the absolute path of the current working directory using the getcwd(3) function\n",
	"prt 				- Prints the return code of the command that was last exectued\n",
	"chpmt [SETTING] [TOGGLE] 	- Changes prompt settings based on SETTING and TOGGLE\n",
	"chclr [SETTING] [COLOR] [BOLD] - Changes prompt colors based on SETTING, COLOR, and BOLD\n",
	"jobs 				- Prints out all jobs running in background, their name, PID, and job number\n",
	"fg PID|JID 			- Take specified job number and resume execution in foreground\n",
	"bg PID|JID 			- Take specified job number and resume execution in background\n",
	"kill [signal] PID|JID 		- Send specified signal or SIGTERM by default to process specified by PID|JID\n",
	"disown [PID|JID] 		- Delete specified job from job list. Delete all if ID not specified\n",
};

const char *STATUS[] = {
	"Running",
	"Stopped",
	"Done",
	"Disowned",
	"Terminated",
};

/* TEST REFACTOR */

// some externs like PRT
extern int ReturnCode;
extern int NumCommands;

// FOR DIRECTORY HANDLING
extern char PreviousDIR[4096];
extern char CurrentDIR[4096];
extern char Prompt[4096];

//	FOR PROMPT AND COLORS
extern bool Machine_On;
extern bool User_On;
extern int Machine_Color;
extern int User_Color;
extern char User[4096];
extern char Machine[4096];


// FOR JOB CONTROL STUFF
extern Job_t *ForegroundJob;
extern Job_t *JobList;
extern pid_t SPID;


//** SIGNAL HANDLERS **//
void signal_child(int sig);
void signal_suspend(int sig);
void signal_kill(int sig);

pid_t Fork();

/* 	BUILTIN COMMANDS */

bool CheckFileExists(char *file);
int CheckBuiltinCommands(const char *cmd);
void ExecuteBuiltinCommands(int opt, char **argv, int argc);

// INITIALIZATION
void Init();

int PrintHelpMenu();
int PresentDIR();
int ChangeDirectory(char *path);
int Chdir(char *path);
int Chpmt(char **argv, int argc);
int Chclr(char **argv, int argc);
void UpdatePrompt();
void PrintPrompt();

// COMMANDS LINE PARSING

char *ParseCommandLine(char *cmd, int *bg);
int ReplaceWhitespace(char *cmd, char **argv);
int AtoID(char *num, int *isPID);


//	JOBS STUFF

Job_t *CreateJob(char *cmd);
void FreeJob(Job_t *job);

// PROCESS STUFF

void AddProcess(Process_t *process, Job_t *job);
Process_t *CreateProcess(char *cmd);

// EXECTUING JOBS

bool Pipeline(Job_t *job);
void ExecuteCommand(Job_t *job, int bg);
void ExecuteProcess(Process_t *process);
void WaitForJob(Job_t *job);
bool JobStopped(Job_t *job);
bool JobFinished(Job_t *job);
void AddJobToList(Job_t *job);
void PrintJobList();
void PrintJob(Job_t *job);
Job_t *FindJob(pid_t pid, int jid, int opt);
void FreeJobList();
void SetProcessStatus(Job_t *job, pid_t pid, int status);
void CheckJobListStatus();

// Bringing back JOBS T_T
int Fg(char *num, int argc);
int Bg(char *num, int argc);
int Disown(char *num, int argc);
int Kill(char **argv, int argc);


// FILE STUFF
int Open(char *f, int opt, mode_t mode);
int Close(int fd);
int Dup2(int oldfd, int newfd);
void CloseStandardDescriptors(int fd_in, int fd_out, int fd_err);


// KEY BINDING STUFF
int Readline_Help(int count, int key);
int Readline_SetPID(int count, int key);
int Readline_GetPID(int count, int key);
int readline_SFISH(int count, int key);

#endif




/*

	ERRRRRRRORS

	- Long pipes causes memory leaks because SIGCHLD is being caught in handler occasionally and not waitpid
	- Remember to change pid of job to most recent one
	- Check TERMIANTION STATUS FOR SIGCHLD
	- FG/BG on already running processes


	// MOST IMPORTANT LOGIC ERROR TO FIX IF YOU CHOOSE TO FIX IT

	- bg acts as fg in that it catches CTRL - C and CTRL - Z

*/