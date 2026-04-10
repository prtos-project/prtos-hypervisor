/*
 * FILE: prtos_manager.c
 *
 * PRTOS Partition Manager - Main command dispatcher
 *
 */

#include "common.h"
#include "prtos_manager.h"

static int CmdHelp(char *line);
static int CmdQuit(char *line);

int CmdList(char *line);
int CmdPartition(char *line);
int CmdPlan(char *line);
int CmdWriteConsole(char *line);

int nopart = -1;

/* cmdTab is lexicographically sorted by cmd field */
static struct CmdTab {
    int (*func) (char *);
    char cmd[64];
    char help[128];
    char usage[128];
} cmdTab[] = {
    { CmdHelp,         "help",      "Display this help",             "help [command]" },
    { CmdList,         "list",      "List partitions",               "list [partitionid]" },
    { CmdPartition,    "partition", "Control partition state",       "partition [halt|reset|resume|status|suspend] [partitionid]" },
    { CmdPlan,         "plan",      "Manage hypervisor sched plan",  "plan [id]" },
    { CmdQuit,         "quit",      "Exit prtos_manager",            "quit" },
    { CmdWriteConsole, "write",     "Write to the PRTOS Console",    "write [string]" },
};

static char *cmdList[NELEM(cmdTab)];

char *Trim(char *line)
{
    if (!line)
        return 0;
    while (line[0] == ' ')
        line++;
    return line;
}

int SplitLine(char *line, char *arg[], int nargs, int tokenize)
{
    int i;
    char *p;

    if (!line || !arg || !nargs)
        return -1;

    arg[0] = line;
    for (i = 0; i < nargs - 1; i++) {
        arg[i] = Trim(arg[i]);
        if (!arg[i])
            break;

        arg[i+1] = strchr(arg[i], ' ');

        if (!arg[i+1])
            break;

        if (tokenize) {
            p = strchr(arg[i+1], ' ');
            if (p)
                *p = '\0';
        }

        arg[i+1]++;
    }

    return i;
}

int GetCommandIndex(char *line, char *cmdlist[], int nelem)
{
    int i, n, narg;
    char *arg[1];

    narg = SplitLine(line, arg, NELEM(arg), 0);
    if (narg < 0)
        return -1;

    n = strlen(arg[0]);
    if (narg > 1)
        n = arg[1] - arg[0];

    for (i = 0; i < nelem; i++) {
        if (strncmp(cmdlist[i], arg[0], n) == 0)
            break;
    }

    if (i == nelem)
        return -1;

    return i;
}

char *FindCommand(void *cmdfunc)
{
    int i;

    for (i = 0; i < (int)NELEM(cmdTab); i++) {
        if (cmdTab[i].func == cmdfunc)
            break;
    }

    if (i == (int)NELEM(cmdTab))
        return "unknown";

    return cmdTab[i].cmd;
}

int CheckPartition(int id, void *cmdfunc)
{
    char *cmd;
    int self = prtos_hv_get_partition_self();

    cmd = FindCommand(cmdfunc);

    if (id < 0) {
        prtosprintf("Error: %s: partitionId not set %d\n", cmd, id);
        return -1;
    }

    if (self < 0) {
        prtosprintf("Error: %s: invalid PRTOS_PARTITION_SELF %d\n", cmd, self);
        return -1;
    }

    if (id == self) {
        prtosprintf("Error: %s: invalid partitionId equal PRTOS_PARTITION_SELF %d\n", cmd, id);
        return -1;
    }

    return 0;
}

static int CmdHelp(char *line)
{
    int i, j;
    int maxlen;

    if (line) {
        i = GetCommandIndex(line, cmdList, NELEM(cmdList));
        if (i < 0) {
            prtosprintf("Error: command not found: %s\n", line);
            return -1;
        }
        prtosprintf("usage: %s\n", cmdTab[i].usage);
        return 0;
    }

    prtosprintf("PrtosManager commands help:\n");
    for (i = 0; i < (int)NELEM(cmdTab); i++) {
        prtosprintf("  %s ", cmdTab[i].cmd);
        maxlen = 10;
        for (j = strlen(cmdTab[i].cmd); j < maxlen; j++)
            prtosprintf(" ");
        prtosprintf("%s\n", cmdTab[i].help);
    }
    prtosprintf("Type 'help command' for more information on a specific command.\n");
    return 0;
}

static int CmdQuit(char *line)
{
    (void)line;
    return 0;
}

/* Input/output */

static PrtosManagerDevice_t *device;

static int ReadLine(char *line, int length)
{
    int i;

    memset(line, '\0', length);
    prtosprintf("\rprtos%d> ", prtos_hv_get_partition_self());
    for (i = 0; i < length; i++) {
        while (device->read(&line[i], 1) != 1)
            continue;

        if (device->flags & DEVICE_FLAG_COOKED) {
            if (i > 0 && (line[i] == '\b' || line[i] == 0x7f)) {
                line[i--] = '\0';
                line[i--] = '\0';
                device->write("\b \b", 3);
                continue;
            }
            device->write(&line[i], 1);
        }

        if (line[i] == '\n' || line[i] == '\r') {
            if (device->flags & DEVICE_FLAG_COOKED)
                device->write("\n", 1);

            line[i] = '\0';
            break;
        }
    }
    return i;
}

int prtosprintf(char const *fmt, ...)
{
    int len;
    char str[512];
    va_list args;

    memset(str, '\0', sizeof(str));

    va_start(args, fmt);
    len = vsnprintf(str, sizeof(str), fmt, args);
    va_end(args);

    device->write(str, len);
    return len;
}

int PrtosManager(PrtosManagerDevice_t *dev)
{
    int i, n, ret;
    char *arg[2];
    char *line, buff[256];

    device = dev;
    if (device->init)
        device->init();

    for (i = 0; i < (int)NELEM(cmdList); i++)
        cmdList[i] = cmdTab[i].cmd;

    prtosprintf("PRTOS Partition manager running on partition %d\n", prtos_hv_get_partition_self());
    CmdList(0);

    while (1) {
        line = buff;
        n = ReadLine(line, sizeof(buff));
        if (n <= 0)
            continue;

        n = SplitLine(line, arg, NELEM(arg), 1);
        i = GetCommandIndex(arg[0], cmdList, NELEM(cmdList));
        if (i < 0 || n < 0) {
            prtosprintf("Error: command \"%s\" not found, type \"help\"\n", arg[0]);
            continue;
        }

        ret = cmdTab[i].func(arg[1]);
        prtosprintf("Status: %s returned %d \"%s\"\n",
                    (ret >= 0) ? "Done" : "Error", ret, ErrorToStr(ret));

        if (strncmp(cmdTab[i].cmd, "quit", 4) == 0)
            break;
    }

    return 0;
}
