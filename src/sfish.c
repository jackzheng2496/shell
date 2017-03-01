#include "sfish.h"

int ReturnCode;
int NumCommands;

char PreviousDIR[];
char CurrentDIR[];

bool Machine_On;
bool User_On;

int Machine_Color;
int User_Color;

char User[];
char Machine[];
char Prompt[];

Job_t *ForegroundJob;
Job_t *JobList;

pid_t SPID;

int main(int argc, char **argv)
{
    rl_catch_signals = 0;

    char *cmd;
    int bg;

    //INITIALIZATION
    Init();

    while ((cmd = readline(Prompt)) != NULL) {
        bg = 0;
        // PRINT OUT INFO ABOUT BACKGROUND JOBS THAT HAVE COMPLETED
        CheckJobListStatus();
        // USER JUST ENTERED NEWLINE
        if (strlen(cmd) == 0) {
            free(cmd);
            continue;
        }

        if (strcmp("exit", cmd) == 0) {
            free(cmd);
            break;
        }

        char *modified_cmd = ParseCommandLine(cmd, &bg);
        free(cmd); // DONT NEED THIS ANYMORE

        if (modified_cmd == NULL) {
            fprintf(stderr, "Invalid input\n");
            continue;
        }

        Job_t *job = CreateJob(modified_cmd); //FREE MODIFIED_CMD AFTER THIS
        free(modified_cmd);

        //NOW WE HAVE THE PARSED JOB/PROCESSES
        if (Pipeline(job)) {
            FreeJob(job);
            continue;
        }

        ExecuteCommand(job, bg);

        if (job->current_status == 4 || JobFinished(job))
            FreeJob(job);
    }

    CloseStandardDescriptors(STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);
}

void signal_child(int sig)
{
    sigset_t mask, prev_mask;
    pid_t pid;
    int status;
    Job_t *job;

    sigemptyset(&mask);
    sigaddset(&mask, SIGTSTP);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGCHLD);

    if ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        sigprocmask(SIG_BLOCK, &mask, &prev_mask);

        job = FindJob(pid, 0, 1);

        // Exited  check for terminated also
        if (WIFEXITED(status)) {

            if (job)
                job->current_status = 2;
        }

        if (job)
            job->current_status = 2;

        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    }
}

void signal_suspend(int sig)
{
    sigset_t mask, prev_mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGTSTP);
    sigaddset(&mask, SIGINT);

    sigprocmask(SIG_BLOCK, &mask, &prev_mask);

    if (ForegroundJob) {
        ForegroundJob->current_status = 1;

        // kill(ForegroundJob->pgid, SIGSTOP);
        kill(ForegroundJob->pgid, SIGSTOP);
    }

    sigprocmask(SIG_SETMASK, &prev_mask, NULL);
}

void signal_kill(int sig)
{
    sigset_t mask, prev_mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGTSTP);
    sigaddset(&mask, SIGINT);

    sigprocmask(SIG_BLOCK, &mask, &prev_mask);

    if (ForegroundJob) {
        ForegroundJob->current_status = 2;

        // kill(ForegroundJob->pgid, SIGINT);
        killpg(ForegroundJob->pgid, SIGINT);
    }

    sigprocmask(SIG_SETMASK, &prev_mask, NULL);
}

int Readline_Help(int count, int key)
{
    // Print newline
    rl_crlf();

    //Print out help menu
    PrintHelpMenu();

    // Print prompt on next line
    rl_on_new_line();
    return 0;
}

int Readline_SetPID(int count, int key)
{
    rl_crlf();

    SPID = JobList ? JobList->pid : -1;

    rl_on_new_line();
    return 0;
}

int Readline_GetPID(int count, int key)
{
    Job_t *job;

    rl_crlf();

    if (SPID != -1) {
        if ((job = FindJob(SPID, 0, 3))) {

            errno = 0;
            if (kill(job->pgid, SIGTERM) == -1)
                fprintf(stderr, "sfish: kill: error in kill errno = %d\n", errno);
            else {
                fprintf(stdout, "[%d] %d terminated by SIGTERM\n", job->jid, job->pid);

                job->current_status = 4;
                CheckJobListStatus();
            }
        } else {
            fprintf(stdout, "SPID does not exist and has been set to -1\n");
        }

        SPID = -1;
    } else
        fprintf(stdout, "SPID is %d Set SPID using Ctrl-B\n", SPID);

    rl_on_new_line();
    return 0;
}

int readline_SFISH(int count, int key)
{
    Job_t *temp;

    rl_crlf();
    fprintf(stdout, "sfish info\n");
    fprintf(stdout, "----Info----\n"
                    "help\n"
                    "prt\n"
                    "----CTRL----\n"
                    "cd\n"
                    "chclr\n"
                    "chpmt\n"
                    "pwd\n"
                    "exit\n"
                    "----Job Control----\n"
                    "bg\n"
                    "fg\n"
                    "disown\n"
                    "jobs\n"
                    "----Number of Commands Run----\n"
                    "%d\n"
                    "----Process Table----\n", NumCommands);

    if ((temp = JobList))
        fprintf(stdout, "PGID\tPID\tTIME\tCMD\n");

    while (temp) {
        fprintf(stdout, "%d\t%d\t%s\t%s\n", temp->pgid, temp->pid, temp->time_created, temp->cmd);
        temp = temp->next_job;
    }

    rl_on_new_line();
    return 0;
}


void Init()
{
    //binding some keys
    rl_bind_keyseq("\\C-h", Readline_Help);
    rl_bind_keyseq("\\C-b", Readline_SetPID);
    rl_bind_keyseq("\\C-g", Readline_GetPID);
    rl_bind_keyseq("\\C-p", readline_SFISH);
    SPID = -1;

        // INSTALL SIGNAL HANDLERS
    if (signal(SIGCHLD, signal_child) == SIG_ERR)
        fprintf(stderr, "Error installing SIGCHLD\n");

    if (signal(SIGTSTP, signal_suspend) == SIG_ERR)
        fprintf(stderr, "Error installing SIGTSTP\n");

    if (signal(SIGINT, signal_kill) == SIG_ERR)
        fprintf(stderr, "Error installing SIGINT\n");

    //setting up default values
    ReturnCode = NumCommands = 0;
    Machine_On = User_On = true;
    User_Color = Machine_Color = BWHITE;


    //Clear out initial buffers
    memset(Prompt, 0, 4096);
    memset(User, 0, 4096);
    memset(Machine, 0, 4096);
    memset(CurrentDIR, 0, 4096);
    memset(PreviousDIR, 0, 4096);

    gethostname(Machine, 4096);
    strcpy(User, getenv("USER"));

    if (Chdir(getenv("HOME")) == -1)
        fprintf(stderr, "sfish: chdir: change to home error\n");

    UpdatePrompt();
}

/* DOES MOST OF THE HEAVY PARSING */
char *ParseCommandLine(char *cmd, int *bg)
{
    int newlen;

    char *temp_cmd;
    char *iterator;
    char *modified_buffer;

    bool isspace = false;

    /* COUNT LENGTH OF STRING PLUS THE SPECIAL CHARACTERS */
    newlen = 0;
    temp_cmd = cmd;
    while (*temp_cmd) {
        if (*temp_cmd == '>' || *temp_cmd == '<' || *temp_cmd == '|')
            newlen += 2;  // make big enough buffer to put spaces

        newlen++;
        temp_cmd++;
    }

    modified_buffer = malloc(newlen + 1);
    iterator = modified_buffer;
    temp_cmd = cmd;

    /* REMOVE ALL LEADING WHITESPACE */
    while (*temp_cmd == ' ')
        temp_cmd++;

    /* COPY CHARACTERS INTO NEW BUFFER */
    while (*temp_cmd && *temp_cmd != '&') {
        if (*temp_cmd == ' ' && isspace)
            temp_cmd++;
        else {
            if (*temp_cmd == '>' || *temp_cmd == '<' || *temp_cmd == '|') {
                if (iterator == modified_buffer) {
                    free(modified_buffer);
                    return NULL;
                } else if (*(iterator-1) != ' ' && *(iterator-1) != '2') {
                    *iterator++ = ' ';
                }
            }

            *iterator++ = *temp_cmd;

            isspace = *temp_cmd == ' ';

            if (*temp_cmd == '>' || *temp_cmd == '<' || *temp_cmd == '|') {
                *iterator++ = ' ';
                isspace = true;
            }

            temp_cmd++;
        }
    }

    if (*temp_cmd == '&')
        *bg = 1;
    
    if (*(iterator - 1) == ' ')
        iterator--;

    *iterator = '\0';

    return modified_buffer;
}

int ReplaceWhitespace(char *cmd, char **argv)
{
    int args;

    args = 0;
    while (*cmd) {
        while (*cmd == ' ' || *cmd == '\t')
            *cmd++ = '\0';

        *argv++ = cmd;
        args++;

        while (*cmd != ' ' && *cmd != '\t' && *cmd != '\0')
            cmd++;
    }

    if (strlen(*(argv - 1)) == 0) {
        args--;
        argv--;
    }

    *argv = NULL;

    return args;
}

bool CheckFileExists(char *file)
{
    struct stat st;
    return stat(file, &st) == 0;
}

Job_t *CreateJob(char *cmd)
{
    Job_t *new_job;

    char *delim;
    char *tokens;

    new_job = malloc(sizeof(Job_t));
    memset(new_job, 0, sizeof(Job_t));

    new_job->cmd = strdup(cmd);

    delim = "|";
    tokens = strtok(cmd, delim);
    
    while (tokens) {
        /* EACH ONE OF THESE TOKENS SHOULD BE PROCESSES */
        AddProcess(CreateProcess(tokens), new_job);

        tokens = strtok(NULL, delim);
    }

    return new_job;
}

void FreeJob(Job_t *job)
{
    Process_t *pt = job->process_head;

    while (pt) {
        Process_t *next = pt->next_process;
        free(pt->process_name);
        free(pt->process_name_copy);
        free(pt->argv);
        free(pt);

        pt = next;
    }

    free(job->cmd);
    free(job->time_created);
    free(job);
}

Process_t *CreateProcess(char *cmd)
{
    Process_t *new_process;

    new_process = malloc(sizeof(Process_t));
    memset(new_process, 0, sizeof(Process_t));

    new_process->process_name = strdup(cmd);
    new_process->process_name_copy = strdup(cmd);

    new_process->fd_in = STDIN_FILENO;
    new_process->fd_out = STDOUT_FILENO;
    new_process->fd_err = STDERR_FILENO;

    new_process->argv = malloc(sizeof(char *) * 128); //CAREFUL

    new_process->argc = ReplaceWhitespace(new_process->process_name_copy, new_process->argv);

    new_process->isBuiltin = CheckBuiltinCommands(*(new_process->argv));

    return new_process;
}

void AddProcess(Process_t *process, Job_t *job)
{
    if (job->process_head == NULL)
        job->process_head = process;
    else {
        Process_t *temp = job->process_head;

        while (temp->next_process != NULL)
            temp = temp->next_process;

        temp->next_process = process;
    }
}

bool Pipeline(Job_t *job)
{
    Process_t *process;

    char **arg, *stop_position, *file_in, *file_out, *file_err;
    bool error;

    error = false;
    stop_position = file_in = file_out = file_err = NULL;
    process = job->process_head;

    while (process) {

        arg = process->argv;
        while (*arg) {
            if (!strcmp(*arg, "<")) {
                file_in = *(arg + 1);

                if (!file_in || !CheckFileExists(file_in)) {
                    error = true;
                    break;
                }

                if (!stop_position)
                    stop_position = *arg;

            } else if (!strcmp(*arg, ">")) {
                file_out = *(arg + 1);

                if (process->fd_out != STDOUT_FILENO) 
                    Close(process->fd_out);

                // process->fd_out = Open(file_out, FILEPERMISSION, FILEMODE);
                if ((process->fd_out = open(file_out, FILEPERMISSION, FILEMODE)) == -1) {
                    error = true;
                    break;
                }

                if (!stop_position)
                    stop_position = *arg;

            } else if (!strcmp(*arg, "2>")) {
                file_err = *(arg + 1);

                if (process->fd_err != STDERR_FILENO)
                    Close(process->fd_err);

                if ((process->fd_err = open(file_err, FILEPERMISSION, FILEMODE)) == -1) {
                    error = true;
                    break;
                }

                if (!stop_position)
                    stop_position = *arg;
            }

            arg++;
        }

        if (error) {
            int old_stderr = dup(STDERR_FILENO);

            if (process->fd_out != STDOUT_FILENO)
                Close(process->fd_out);

            if (process->fd_in != STDIN_FILENO)
                Close(process->fd_in);

            if (process->fd_err != STDERR_FILENO) {
                Dup2(process->fd_err, STDERR_FILENO);
            }

            fprintf(stderr, "sfish: %s: No such file or directory\n", file_in);

            Dup2(old_stderr, STDERR_FILENO);

            return error;
        }

        if (file_in)
            process->fd_in = open(file_in, O_RDONLY);

        if (process->next_process)
            pipe(process->fds);

        // Setting items after redirection symbols to NULL
        arg = process->argv;
        while (*arg) {
            if (*arg == stop_position) {
                *arg = NULL;
                break;
            }
            arg++;
        }

        stop_position = NULL;
        process = process->next_process;
    }

    return error;
}

void ExecuteCommand(Job_t *job, int bg)
{
    Process_t *process;
    struct stat t_stat;
    struct tm *timeinfo;
    char buffer[256];

    //increment number of commands, some stats stuff
    NumCommands++;

    process = job->process_head;

    while (process) {
        process->current_status = 0; // Running

        if (process->isBuiltin == CD    ||
            process->isBuiltin == CHPMT ||
            process->isBuiltin == CHCLR ||
            process->isBuiltin == FG    ||
            process->isBuiltin == BG    ||
            process->isBuiltin == KILL  ||
            process->isBuiltin == DISOWN) {

            job->interactive = 1;
            ExecuteBuiltinCommands(process->isBuiltin, process->argv, process->argc);

        } else {

            process->pid = Fork();

            // Some stat stuff
            if (process->pid != -1 && !job->time_created) {
                sprintf(buffer, "/proc/%d", process->pid);
                stat(buffer, &t_stat);
                timeinfo = localtime(&t_stat.st_ctime);
                strftime(buffer, 256, "%H:%M", timeinfo);
                job->time_created = strdup(buffer);
            }

            if (process->pid == 0) {

                //Put back in default signal handlers

                if (!job->pgid) job->pgid = getpid();
                setpgid(getpid(), job->pgid);

                // ignore signals????
                ExecuteProcess(process);

            } else if (process->pid == -1) {
                job->current_status = 4;

                if (process->fd_in != STDIN_FILENO)
                    Close(process->fd_in);

                if (process->fd_out != STDOUT_FILENO)
                    Close(process->fd_out);

                if (process->fd_err != STDERR_FILENO)
                    Close(process->fd_err);

                return;

            } else {
                // SET GROUP ID FOR PROCESSES
                if (!job->pgid) {
                    job->pgid = process->pid;
                }

                // SET JOB PID TO LATEST PROCESS IN PROCESS LIST
                job->pid = process->pid;

                setpgid(process->pid, job->pgid);
                // setpgid(process->pid, (~job->pgid)+1); /// just dont set it as job->pgid
                // setpgid(process->pid, 500);
                // setpgid(process->pid, 0);
                // setpgid(0,0);
                // setpgid(getpid(), job->pgid);   // wc -l works and all the others, but background doesnt work because same pgid

                if (process->fd_in != STDIN_FILENO)
                    Close(process->fd_in);

                if (process->fd_out != STDOUT_FILENO)
                    Close(process->fd_out);

                if (process->fd_err != STDERR_FILENO)
                    Close(process->fd_err);

                //For Piping
                if (process->next_process) {
                    process->next_process->fd_in = process->fds[0];
                    Close(process->fds[1]);
                }
            }
        }

        process = process->next_process;
    }

    if (job->interactive) {
        /* For the builtins that do not need forking */


    } else if (bg) {
        // /* BACKGROUND JOB SHOULD JUST LET IT RUN */
        // fprintf(stderr, "Don't Wait PGID: %d Parent PGID: %d \n", job->pgid, getpgrp());
        // fprintf(stderr, "Foreground group: %d\n", tcgetpgrp(STDIN_FILENO));
        job->bg = 1;
        AddJobToList(job);
        fprintf(stdout, "[%d] %d\n", job->jid, job->pgid);

    } else {
        /* FOREGROUND WAIT FOR JOB TO FINISH OR BE KILLED/SUSPENDED */
        // THIS IS THE SKETCHIEST THING FOR THIS HW IDK IF ITS EVEN CORRECT
        signal(SIGTTOU, SIG_IGN);
        tcsetpgrp(STDIN_FILENO, job->pgid);

        ForegroundJob = job;
        // fprintf(stderr, "Waiting for Job: '%s'\n", job->cmd);
        WaitForJob(job);
        ForegroundJob = NULL;

        tcsetpgrp(STDIN_FILENO, getpgrp());
        signal(SIGTTOU, SIG_DFL);
    }

    // fprintf(stderr, "Exited ExecuteCommand\n");
}

void ExecuteProcess(Process_t *process)
{
    // fprintf(stderr, "here boys\n");
    if (process->fd_in != STDIN_FILENO) {
        Dup2(process->fd_in, STDIN_FILENO);
    }

    if (process->fd_out != STDOUT_FILENO) {
        Dup2(process->fd_out, STDOUT_FILENO);
    }

    if (process->fd_err != STDERR_FILENO) {
        Dup2(process->fd_err, STDERR_FILENO);
    }

    if (process->next_process) {
        dup2(process->fds[1], STDOUT_FILENO);
        Close(process->fds[0]);
        Close(process->fds[1]);
    }

    if (process->isBuiltin != NOTBUILTIN) {
        ExecuteBuiltinCommands(process->isBuiltin, process->argv, process->argc);

        CloseStandardDescriptors(process->fd_in, process->fd_out, process->fd_err);
        CloseStandardDescriptors(STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);

        _exit(0);
    }

    if (execvp(process->argv[0], process->argv) < 0) {
        fprintf(stderr, "%s: command not found\n", process->argv[0]);

        CloseStandardDescriptors(process->fd_in, process->fd_out, process->fd_err);
        CloseStandardDescriptors(STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);

        _exit(127);
    }
}

void WaitForJob(Job_t *job)
{
    sigset_t mask, prev_mask;
    pid_t pid;
    int status;

    status = 0;

    sigemptyset(&mask);
    sigaddset(&mask, SIGTSTP);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGCHLD);

    do {
        pid = waitpid(job->pgid, &status, WUNTRACED);
        // fprintf(stderr, "Child: '%d' Sigchld Status: %d %d\n", pid, WEXITSTATUS(status), status);

        sigprocmask(SIG_BLOCK, &mask, &prev_mask);

        if (WIFEXITED(status)) {
            SetProcessStatus(job, pid, 2);
            ReturnCode = WEXITSTATUS(status);
        }

        if (WIFSTOPPED(status))
            SetProcessStatus(job, pid, 1);

        sigprocmask(SIG_SETMASK, &prev_mask, NULL);

    } while (pid > 0 && !JobFinished(job) && !JobStopped(job));

    // pid > -

    if (JobStopped(job) && job->current_status != 2) {      
        // fprintf(stderr, "Job: '%s' Was Stopped\n", job->cmd);
        job->current_status = 1;
        AddJobToList(job);
        fprintf(stdout, "[%d] %d Stopped\n", job->jid, job->pid);
    }

    if (JobFinished(job) || job->current_status == 2) {
        job->current_status = 2;
        // fprintf(stderr, "Job: '%s' Finished Executing\n", job->cmd);
    }
}

int AtoID(char *num, int *isPID)
{
    int idValue;

    if (!num)
        return -1;

    if (*num == '%') {
        *isPID = 2;
        num++;
    }

    idValue = atoi(num);
    return idValue;
}

int Fg(char *num, int argc)
{
    Job_t *job;
    int idValue, isPID;

    idValue = 0;
    isPID = 1;

    if (!num || argc > 2) {
        fprintf(stderr, "Usage: fg PID|JID\n");
        return -1;
    }

    // UNSAFE BUT O WELL
    if ((idValue = AtoID(num, &isPID)) == 0) {
        fprintf(stderr, "Usage: fg PID|JID\n");
        return -1;
    }

    job = FindJob((pid_t) idValue, idValue, isPID);

    if (!job) {
        fprintf(stderr, "Job ID: %d was not found\n", idValue);
        return -1;
    }

    errno = 0;
    if (kill(job->pgid, SIGCONT) == -1) {
        fprintf(stderr, "sfish: kill: error in kill errno = %d\n", errno);
        return -1;
    }
    job->current_status = 0;

    // CHECK FOR PREVIOUS FOREGROUND JOB IF ANY
    ForegroundJob = job;
    WaitForJob(job);
    ForegroundJob = NULL;

    return 0;
}

int Bg(char *num, int argc)
{
    Job_t *job;
    int idValue, isPID;

    idValue = 0;
    isPID = 1;

    if (!num || argc > 2) {
        fprintf(stderr, "Usage: bg PID|JID\n");
        return -1;
    }

    if ((idValue = AtoID(num, &isPID)) == 0) {
        fprintf(stderr, "Usage: bg PID|JID\n");
        return -1;
    }

    job = FindJob((pid_t) idValue, idValue, isPID);

    if (!job) {
        fprintf(stderr, "Job ID: %d was not found\n", idValue);
        return -1;
    }
    // Check to see if running or not
    errno = 0;
    if (kill(job->pgid, SIGCONT) == -1) {
        fprintf(stderr, "sfish: kill: error in kill errno = %d\n", errno);
    }
    job->current_status = 0;

    return 0;
}

int Disown(char *num, int argc)
{
    Job_t *job, *next;
    int idValue, isPID;

    idValue = 0;
    isPID = 1;

    if (argc > 2) {
        fprintf(stderr, "Usage: disown [PID|JID]\n");
        return -1;
    }

    if (!num) {
        if (JobList) {
            job = JobList;
            while (job) {
                next = job->next_job;
                FreeJob(job);
                job = next;
            }
            JobList = NULL;
            fprintf(stdout, "All jobs have been disowned\n");
        } else
            fprintf(stdout, "No jobs\n");
    } else {
        if ((idValue = AtoID(num, &isPID)) == 0) {
            fprintf(stderr, "Usage: disown [PID|JID]\n");
            return -1;
        }

        job = FindJob((pid_t) idValue, idValue, isPID);

        if (!job) {
            fprintf(stderr, "Job ID: %d was not found\n", idValue);
            return -1;
        }

        job->current_status = 3;
        CheckJobListStatus();
    }

    return 0;
}

int Kill(char **argv, int argc)
{
    char *ID;
    int idValue, isPID, sig, res;
    Job_t *job;

    idValue = 0;
    isPID = 1;
    sig = SIGTERM;

    if (argc > 3) {
        fprintf(stderr, "Usage: kill [signal] PID|JID\n");
        return -1;
    }

    if (argc == 3)
        if ((sig = atoi(argv[1])) == 0 || (sig < 1 || sig > 31)) {
            fprintf(stderr, "Usage: kill [signal] PID|JID\n");
            return -1;
        }

    if (argc == 2)
        ID = argv[1];
    else
        ID = argv[2];

    if ((idValue = AtoID(ID, &isPID)) == 0) {
        fprintf(stderr, "Usage: kill  [signal] PID|JID\n");
        return -1;
    }

    job = FindJob((pid_t) idValue, idValue, isPID);

    if (!job) {
        fprintf(stderr, "Job ID: %d was not found\n", idValue);
        return -1;
    }

    // PrintJob(job);

    errno = 0;
    if ((res = kill(job->pgid, sig)) == -1) {
        fprintf(stderr, "sfish: kill: error in kill errno = %d", errno);
    }

    switch (sig) {
    case SIGSTOP: case SIGTSTP:
    case SIGTTIN: case SIGTTOU:
        job->current_status = 1;
        fprintf(stdout, "[%d] %d stopped by signal %d\n", job->jid, job->pid, sig);
        break;
    case SIGCHLD: case SIGURG:
        fprintf(stdout, "SIGCHLD | SIGURG ignored\n");
        break;
    case SIGCONT:
        fprintf(stdout, "[%d] %d continued by signal %d\n", job->jid, job->pid, sig);
        job->current_status = 0;
        break;
    default:
        fprintf(stdout, "[%d] %d terminated by signal %d\n", job->jid, job->pid, sig);
        job->current_status = 4;
        CheckJobListStatus();
        break;
    }

    return res;
}

void SetProcessStatus(Job_t *job, pid_t pid, int status)
{
    // fprintf(stderr, "Set Status: %d\n", status);
    Process_t *process;

    process = job->process_head;
    while (process) {
        process->current_status = status;
        // if (process->pid == pid) {
        //     fprintf(stderr, "Found!\n");
        //     process->current_status = status;
        //     break;
        // }
        process = process->next_process;
    }
}

bool JobStopped(Job_t *job)
{
    Process_t *process;

    process = job->process_head;
    while (process) {
        if (process->current_status == 2 || process->current_status == 0)
            return false;

        process = process->next_process;
    }

    return true;
}

bool JobFinished(Job_t *job)
{
    Process_t *process;

    process = job->process_head;
    while (process) {
        if (process->current_status != 2)
            return false;

        process = process->next_process;
    }

    return true;
}

void AddJobToList(Job_t *job)
{
    Job_t *temp;

    if (!job || FindJob(job->pgid, 0, 1))
        return;

    if (!JobList) {
        job->jid = 1;
        JobList = job;

    } else {
        temp = JobList;

        while (temp->next_job)
            temp = temp->next_job;

        temp->next_job = job;
        job->jid = temp->jid + 1;
    }
}

Job_t *FindJob(pid_t pid, int jid, int opt)
{
    Job_t *temp = JobList;
    while (temp) {
        if (opt == 1) {
            if (temp->pgid == pid)
                break;
        } else if (opt == 2) {
            if (temp->jid == jid)
                break;
        } else {
            if (temp->pid == pid)
                break;
        }

        temp = temp->next_job;
    }
    return temp;
}

void PrintJobList()
{
    Job_t *temp;

    temp = JobList;
    while (temp) {
        PrintJob(temp);
        temp = temp->next_job;
    }
}

void PrintJob(Job_t *job)
{
    if (job->bg)
        fprintf(stdout, "[%d]   %s\t%d\t%s &\n", job->jid, STATUS[job->current_status], job->pid, job->cmd);
    else
        fprintf(stdout, "[%d]   %s\t%d\t%s\n", job->jid, STATUS[job->current_status], job->pid, job->cmd);
}

void CheckJobListStatus()
{
    Job_t *temp, *prev, *next;

    prev = next = NULL;

    temp = JobList;
    while (temp) {
        next = temp->next_job;

        /* CHECK BOTH BECAUSE OF HOW SIGNALS CAN ARRIVE IN BACKGROUND */
        if (temp->current_status >= 2 || JobFinished(temp)) {
            PrintJob(temp);
            FreeJob(temp);

            if (temp == JobList)
                JobList = next;

            if (prev)
                prev->next_job = next;

            temp = NULL;
        } else {
            prev = temp;
        }
        temp = next;
    }
}

//         FILE STUFF       //
int Close(int fd)
{
    int res;

    errno = 0;
    if ((res = close(fd)) == -1)
        fprintf(stderr, "errno: %d sfish: close error\n", errno);

    return res;
}

int Open(char *f, int opt, mode_t mode)
{
    int res;

    errno = 0;
    if ((res = open(f, opt)) == -1)
        fprintf(stderr, "sfish: errno: %d :open error\n", errno);

    return res;
}

int Dup2(int oldfd, int newfd)
{
    int res;
    errno = 0;
    if ((res = dup2(oldfd, newfd)) == -1)
        fprintf(stderr, "sfish: dup2: errno: %d error in dup2\n", errno);
    else
        Close(oldfd);
    return res;
}


void CloseStandardDescriptors(int fd_in, int fd_out, int fd_err)
{
    Close(fd_in);
    Close(fd_out);
    Close(fd_err);
}

pid_t Fork()
{
    pid_t pid;

    errno = 0;
    if ((pid = fork()) == -1)
        fprintf(stderr, "sfish: fork: error in fork errno = %d\n", errno);
    
    return pid;
}

// Builtin stuff //
int Chdir(char *path)
{
    int res;

    if ((res = chdir(path)))
        fprintf(stderr, "sfish: error with chdir\n");

    return res;
}

int CheckBuiltinCommands(const char *cmd)
{
    if (!strcmp(cmd, "help"))
        return HELP;
    else if (!strcmp(cmd, "exit"))
        return EXIT;  
    else if (!strcmp(cmd, "cd"))
        return CD; 
    else if (!strcmp(cmd, "pwd"))
        return PWD;
    else if (!strcmp(cmd, "prt"))
        return PRT;
    else if (!strcmp(cmd, "chpmt"))
        return CHPMT;
    else if (!strcmp(cmd, "chclr"))
        return CHCLR;
    else if (!strcmp(cmd, "jobs"))
        return JOBS;
    else if (!strcmp(cmd, "fg"))
        return FG;
    else if (!strcmp(cmd, "bg"))
        return BG;
    else if (!strcmp(cmd, "kill"))
        return KILL;
    else if (!strcmp(cmd, "disown"))
        return DISOWN;
    else
        return NOTBUILTIN;
}

void ExecuteBuiltinCommands(int opt, char **argv, int argc)
{
    if (opt == HELP    ||
        opt == PWD     ||
        opt == PRT     ||
        opt == JOBS    ||
        opt == FG      ||
        opt == BG      ||
        opt == KILL    ||
        opt == DISOWN) {

        switch (opt) {
        case HELP:
            ReturnCode = PrintHelpMenu();
            break;
        case PWD:
            ReturnCode = PresentDIR();
            break;
        case PRT:
            fprintf(stdout, "%d\n", ReturnCode);
            ReturnCode = 0;
            break;
        case JOBS:
            PrintJobList();
            ReturnCode = 0;
            break;
        case FG:
            // SHITTY PARSING JUST TRY IT FOR NOW
            ReturnCode = Fg(argv[1], argc);
            break;
        case BG:
            //SHITTY TRY AGAIN TOMORROW
            ReturnCode = Bg(argv[1], argc);
            break;
        case KILL:
            ReturnCode = Kill(argv, argc);
            break;
        case DISOWN:
            ReturnCode = Disown(argv[1], argc);
            break;
        default:
            break;
        }

    } else {

        switch (opt) {
        case CD:
            ReturnCode = ChangeDirectory(argv[1]);
            break;
        case CHPMT:
            ReturnCode = Chpmt(argv, argc);
            break;
        case CHCLR:
            ReturnCode = Chclr(argv, argc);
            break;
        default:
            break;
        }

        if (ReturnCode != -1)
            UpdatePrompt();
        else
            fprintf(stderr, "%s: command not found\n", argv[0]);
    }
}

int PrintHelpMenu()
{
    int i, len;
    for (i = 0, len = LEN(HELPMENU); i < len; i++)
        fprintf(stdout, "%s", HELPMENU[i]);

    return 0;
}

int PresentDIR()
{
    char buffer[4096];
    errno = 0;

    if (getcwd(buffer, 4096))
        fprintf(stdout, "%s\n", buffer);
    else
        fprintf(stdout, "sfish: error with getcwd\n");

    return errno;
}

int ChangeDirectory(char *path)
{
    int res;

    res = 0;
    if (path == NULL || !strcmp(path, "~")) {
        if (!getcwd(PreviousDIR, 4096))
            fprintf(stderr, "sfish: error with getcwd\n");

        res = Chdir(getenv("HOME"));
    } else if (!strcmp(path, "..") || !strcmp(path, ".")) {
        if (!getcwd(PreviousDIR, 4096))
            fprintf(stderr, "sfish: error with getcwd\n");

        res = Chdir(path);
    } else if (!strcmp(path, "-")) {
        if (strlen(PreviousDIR) == 0)
            fprintf(stderr, "sfish: cd: OLDPWD not set\n");
        else {
            if (!getcwd(CurrentDIR, 4096))
                fprintf(stderr, "sfish: error with getcwd\n");
            if ((res = Chdir(PreviousDIR)) != -1) {
                fprintf(stderr, "%s\n", PreviousDIR);
                strcpy(PreviousDIR, CurrentDIR);
            }
        }
    } else {
        if (!getcwd(CurrentDIR, 4096))
            fprintf(stderr, "sfish: error with getcwd\n");
        if ((res = Chdir(path)) == -1)
            fprintf(stderr, "sfish: cd: %s: No such file or directory\n", path);
        else
            strcpy(PreviousDIR, CurrentDIR);
    }

    return res == -1 ? 1 : res;
}

void UpdatePrompt()
{
    int i;
    char *dir_ptr;

    dir_ptr = CurrentDIR;

    if (!getcwd(Prompt, 4096))
        fprintf(stderr, "sfish: error with getcwd\n");

    i = 0;
    if (!strcmp(Prompt, getenv("HOME"))) {
        *dir_ptr++ = '~';
        *dir_ptr = '\0';
    } else {
        if (strstr(Prompt, getenv("HOME"))) {
            *dir_ptr++ = '~';
            i = strlen(getenv("HOME"));
        }

        while ((*dir_ptr++ = *(Prompt + i++)))
            ;
    }

    PrintPrompt();
}

void PrintPrompt()
{
    if (User_On && Machine_On)
        sprintf(Prompt, "sfish-%s%s%s@%s%s%s:[%s]> ", CC[User_Color], User, CC[RESET],
                                                      CC[Machine_Color], Machine, CC[RESET], CurrentDIR);
    else if (User_On)
        sprintf(Prompt, "sfish-%s%s%s:[%s]> ", CC[User_Color], User, CC[RESET], CurrentDIR);
    else if (Machine_On)
        sprintf(Prompt, "sfish-%s%s%s:[%s]> ", CC[Machine_Color], Machine, CC[RESET], CurrentDIR);
    else
        sprintf(Prompt, "sfish:[%s]> ", CurrentDIR);
}

int Chpmt(char **argv, int argc)
{
    if (argc != 3)
        return -1;

    int choice;
    bool set = false;

    choice = -1;

    if (!strcmp(argv[1], "user"))
        choice = 1;
    else if (!strcmp(argv[1], "machine"))
        choice = 2;
    else
        return -1;

    if (!strcmp(argv[2], "0"))
        set = false;
    else if (!strcmp(argv[2], "1"))
        set = true;
    else
        return -1;

    if (choice == 1)
        User_On = set;
    else
        Machine_On = set;

    return 0;
}

int Chclr(char **argv, int argc)
{
    if (argc != 4)
        return -1;

    int choice;
    int temp_color;

    choice = -1;
    temp_color = RESET;

    if (!strcmp(argv[1], "user"))
        choice = 1;
    else if (!strcmp(argv[1], "machine"))
        choice = 2;
    else
        return -1;

    if (!strcmp(argv[2], "red"))
        temp_color = RED;
    else if (!strcmp(argv[2], "blue"))
        temp_color = BLUE;
    else if (!strcmp(argv[2], "green"))
        temp_color = GREEN;
    else if (!strcmp(argv[2], "yellow"))
        temp_color = YELLOW;
    else if (!strcmp(argv[2], "cyan"))
        temp_color = CYAN;
    else if (!strcmp(argv[2], "magenta"))
        temp_color = MAGENTA;
    else if (!strcmp(argv[2], "black"))
        temp_color = BLACK;
    else if (!strcmp(argv[2], "white"))
        temp_color = WHITE;
    else
        return -1;

    if (!strcmp(argv[3], "0"))
        ;
    else if (!strcmp(argv[3], "1"))
        temp_color += BOLD;
    else
        return -1;

    if (choice == 1)
        User_Color = temp_color;
    else
        Machine_Color = temp_color;

    return 0;
}