/*=============================================================================
 *  PROJECT : Chat Message Management System Using Queue, Stack and Linked List
 *  LANGUAGE: C  (ANSI C / C99 - standard library only, no external packages)
 *  BUILD   : gcc -Wall -Wextra -std=c99 -o chat chat_system.c
 *  RUN     : ./chat
 *-----------------------------------------------------------------------------
 *  DATA STRUCTURES USED
 *  --------------------------------------------------------------------------
 *   LINKED LIST : permanent chat history. Grows dynamically, one node per
 *                 message. Head/Tail pointers give O(1) append.
 *   QUEUE       : pending (undelivered) messages. FIFO - the message sent
 *                 first is the message delivered first.
 *   STACK       : undo buffer. LIFO - "Delete Last Message" always removes
 *                 the most recently sent message first.
 *
 *  MEMORY MODEL (important - read before modifying)
 *  --------------------------------------------------------------------------
 *   Every message is stored EXACTLY ONCE, inside a ChatNode of the linked
 *   list. The Queue and the Stack do NOT copy the message; they only hold a
 *   POINTER (reference) to that ChatNode. This means:
 *       - no duplicated text, no risk of the three views going out of sync
 *       - only the linked list is allowed to free() a ChatNode
 *   So deleting a message = unlink from Queue (if still pending)
 *                         + unlink from Stack (the popped node)
 *                         + unlink from Linked List, then free().
 *===========================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/*---------------------------------------------------------------- constants */
#define MAX_TEXT   201          /* 200 characters of message + '\0'          */
#define MAX_NAME    31          /* 30 characters of user name  + '\0'        */
#define MAX_INPUT  256          /* raw input buffer                          */

#define PENDING     0
#define DELIVERED   1

/*=============================================================================
 *  1. LINKED LIST  ->  CHAT HISTORY
 *===========================================================================*/
typedef struct ChatNode {
    int              id;                /* auto-increment message number     */
    char             sender[MAX_NAME];  /* who sent it                       */
    char             text[MAX_TEXT];    /* the message body                  */
    time_t           stamp;             /* timestamp (seconds since epoch)   */
    int              status;            /* PENDING or DELIVERED              */
    struct ChatNode *next;              /* link to the next message          */
} ChatNode;

static ChatNode *histHead = NULL;       /* first message ever sent           */
static ChatNode *histTail = NULL;       /* last  message ever sent (O(1) add)*/

/*=============================================================================
 *  2. QUEUE  ->  PENDING MESSAGES   (FIFO)
 *===========================================================================*/
typedef struct QNode {
    ChatNode     *ref;                  /* reference into the linked list    */
    struct QNode *next;
} QNode;

static QNode *qFront = NULL;            /* dequeue from here                 */
static QNode *qRear  = NULL;            /* enqueue here                      */

/*=============================================================================
 *  3. STACK  ->  UNDO / DELETE LAST   (LIFO)
 *===========================================================================*/
typedef struct SNode {
    ChatNode     *ref;                  /* reference into the linked list    */
    struct SNode *next;
} SNode;

static SNode *sTop = NULL;              /* push and pop happen here          */

/*=============================================================================
 *  COUNTERS  (the "message counter" feature)
 *===========================================================================*/
static int cSent      = 0;              /* total messages ever sent          */
static int cDelivered = 0;              /* total messages delivered          */
static int cDeleted   = 0;              /* total messages undone/deleted     */
static int nHistory   = 0;              /* messages currently in history     */
static int nPending   = 0;              /* messages currently in the queue   */
static int nextId     = 1;              /* next message id to hand out       */

static char currentUser[MAX_NAME] = "User";

/*=============================================================================
 *  SMALL UTILITIES
 *===========================================================================*/

/* malloc that never returns NULL - it aborts instead, so callers stay clean */
static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (p == NULL) {
        printf("\n  [FATAL] Out of memory. Program terminated.\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

/* Safe line input: strips '\n', and flushes the rest of the line if the user
   typed more characters than the buffer can hold. Never overflows.          */
static void readLine(char *buf, int size)
{
    int ch;
    if (fgets(buf, size, stdin) == NULL) {      /* EOF (Ctrl+D / Ctrl+Z)     */
        buf[0] = '\0';
        return;
    }
    if (strchr(buf, '\n') == NULL) {            /* input was too long        */
        while ((ch = getchar()) != '\n' && ch != EOF)
            ;                                   /* discard the leftovers     */
    } else {
        buf[strcspn(buf, "\n")] = '\0';         /* remove the newline        */
    }
}

/* Remove leading and trailing blanks, in place. Returns the same pointer.   */
static char *trim(char *s)
{
    char *start = s;
    size_t len;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    return s;
}

/* Read a menu choice. Returns -1 when the user typed something that is not
   a plain integer, so the caller can print "Invalid choice".                */
static int readChoice(void)
{
    char line[MAX_INPUT];
    int  value;
    readLine(line, sizeof line);
    trim(line);
    if (line[0] == '\0') return -1;
    if (sscanf(line, "%d", &value) != 1) return -1;
    return value;
}

/* Format a timestamp as "28-Sep-2026 14:32:01" into the caller's buffer.    */
static char *fmtTime(time_t t, char *out, size_t n)
{
    struct tm *lt = localtime(&t);
    if (lt == NULL || strftime(out, n, "%d-%b-%Y %H:%M:%S", lt) == 0)
        snprintf(out, n, "(time unavailable)");
    return out;
}

/* Case-insensitive substring search (our own, because strcasestr is not
   part of standard C). Returns index of the match, or -1 if not found.      */
static int ciFind(const char *hay, const char *needle)
{
    size_t hl = strlen(hay), nl = strlen(needle), i, j;
    if (nl == 0 || nl > hl) return -1;
    for (i = 0; i + nl <= hl; i++) {
        for (j = 0; j < nl; j++)
            if (tolower((unsigned char)hay[i + j]) !=
                tolower((unsigned char)needle[j])) break;
        if (j == nl) return (int)i;
    }
    return -1;
}

static void pause_(void)
{
    char dummy[MAX_INPUT];
    printf("\n  Press ENTER to return to the menu...");
    readLine(dummy, sizeof dummy);
}

static void line_(char c, int n)
{
    int i;
    for (i = 0; i < n; i++) putchar(c);
    putchar('\n');
}

static void heading(const char *title)
{
    printf("\n");
    line_('-', 62);
    printf("  %s\n", title);
    line_('-', 62);
}

/*=============================================================================
 *  LINKED LIST OPERATIONS  (chat history)
 *===========================================================================*/

/* Append at the end so history stays in chronological order.  O(1)          */
static void listAppend(ChatNode *m)
{
    m->next = NULL;
    if (histHead == NULL) histHead = histTail = m;
    else { histTail->next = m; histTail = m; }
    nHistory++;
}

/* Unlink one specific node from the list (does NOT free it).  O(n)          */
static int listUnlink(ChatNode *target)
{
    ChatNode *cur = histHead, *prev = NULL;
    while (cur != NULL && cur != target) { prev = cur; cur = cur->next; }
    if (cur == NULL) return 0;                     /* not present            */
    if (prev == NULL) histHead = cur->next;        /* it was the head        */
    else               prev->next = cur->next;
    if (cur == histTail) histTail = prev;          /* it was the tail        */
    cur->next = NULL;
    nHistory--;
    return 1;
}

/*=============================================================================
 *  QUEUE OPERATIONS  (pending messages, FIFO)
 *===========================================================================*/

/* Insert at REAR.  O(1)                                                      */
static void enqueue(ChatNode *m)
{
    QNode *n = (QNode *)xmalloc(sizeof(QNode));
    n->ref  = m;
    n->next = NULL;
    if (qRear == NULL) qFront = qRear = n;         /* queue was empty        */
    else { qRear->next = n; qRear = n; }
    nPending++;
}

/* Remove from FRONT and return the message it referenced.  O(1)             */
static ChatNode *dequeue(void)
{
    QNode    *n;
    ChatNode *m;
    if (qFront == NULL) return NULL;               /* underflow              */
    n = qFront;
    m = n->ref;
    qFront = qFront->next;
    if (qFront == NULL) qRear = NULL;              /* queue became empty     */
    free(n);
    nPending--;
    return m;
}

/* Take a specific message out of the middle of the queue (needed when that
   message is deleted by Undo before it was ever delivered).  O(n)           */
static int queueUnlinkRef(ChatNode *target)
{
    QNode *cur = qFront, *prev = NULL;
    while (cur != NULL && cur->ref != target) { prev = cur; cur = cur->next; }
    if (cur == NULL) return 0;
    if (prev == NULL) qFront = cur->next;
    else               prev->next = cur->next;
    if (cur == qRear) qRear = prev;
    free(cur);
    nPending--;
    return 1;
}

/*=============================================================================
 *  STACK OPERATIONS  (undo buffer, LIFO)
 *===========================================================================*/

/* Push on TOP.  O(1)                                                         */
static void push(ChatNode *m)
{
    SNode *n = (SNode *)xmalloc(sizeof(SNode));
    n->ref  = m;
    n->next = sTop;
    sTop    = n;
}

/* Pop from TOP and return the message it referenced.  O(1)                  */
static ChatNode *pop(void)
{
    SNode    *n;
    ChatNode *m;
    if (sTop == NULL) return NULL;                 /* underflow              */
    n    = sTop;
    m    = n->ref;
    sTop = n->next;
    free(n);
    return m;
}

static ChatNode *peek(void) { return (sTop == NULL) ? NULL : sTop->ref; }

/*=============================================================================
 *  DISPLAY HELPERS
 *===========================================================================*/
static void printRowHeader(void)
{
    printf("  %-5s %-21s %-12s %-10s %s\n",
           "ID", "TIMESTAMP", "SENDER", "STATUS", "MESSAGE");
    printf("  ");
    line_('-', 60);
}

static void printRow(const ChatNode *m)
{
    char ts[32], id[8];
    fmtTime(m->stamp, ts, sizeof ts);
    snprintf(id, sizeof id, "#%03d", m->id);
    printf("  %-5s %-21s %-12s %-10s %s\n",
           id, ts, m->sender,
           (m->status == DELIVERED) ? "DELIVERED" : "PENDING",
           m->text);
}

static void statusBar(void)
{
    printf("  Sent: %-4d Delivered: %-4d Pending: %-4d History: %-4d Deleted: %-4d\n",
           cSent, cDelivered, nPending, nHistory, cDeleted);
}

static void showMenu(void)
{
    printf("\n");
    line_('=', 62);
    printf("               C H A T   M E S S A G E   S Y S T E M\n");
    printf("                     Logged in as : %s\n", currentUser);
    line_('=', 62);
    statusBar();
    line_('=', 62);
    printf("   1. Send Message              5. Delete Last Message (Undo)\n");
    printf("   2. Receive/Deliver Message   6. View Pending Messages\n");
    printf("   3. View Chat History         7. Clear Chat\n");
    printf("   4. Search Message            8. Exit\n");
    line_('=', 62);
    printf("  Enter choice: ");
}

/*=============================================================================
 *  FEATURE 1 : SEND MESSAGE
 *  A new message is (a) appended to the linked list  -> history
 *                   (b) enqueued at the rear         -> waiting for delivery
 *                   (c) pushed on the stack          -> can be undone
 *===========================================================================*/
static void sendMessage(void)
{
    char      buf[MAX_INPUT], ts[32];
    ChatNode *m;

    heading("SEND MESSAGE");
    printf("  Type your message (max %d chars, blank to cancel)\n", MAX_TEXT - 1);
    printf("  %s > ", currentUser);
    readLine(buf, sizeof buf);
    trim(buf);

    if (buf[0] == '\0') {
        printf("\n  [!] Empty message discarded. Nothing was sent.\n");
        return;
    }

    m = (ChatNode *)xmalloc(sizeof(ChatNode));
    m->id     = nextId++;
    m->stamp  = time(NULL);
    m->status = PENDING;
    m->next   = NULL;
    strncpy(m->sender, currentUser, MAX_NAME - 1); m->sender[MAX_NAME - 1] = '\0';
    strncpy(m->text,   buf,         MAX_TEXT - 1); m->text[MAX_TEXT - 1]   = '\0';

    listAppend(m);          /* 1. linked list : permanent history            */
    enqueue(m);             /* 2. queue       : waiting to be delivered      */
    push(m);                /* 3. stack       : newest is on top for undo    */
    cSent++;

    printf("\n  [OK] Message #%03d sent at %s\n", m->id, fmtTime(m->stamp, ts, sizeof ts));
    printf("       \"%s\"\n", m->text);
    printf("       -> added to HISTORY (linked list)\n");
    printf("       -> added to QUEUE at REAR (position %d, awaiting delivery)\n", nPending);
    printf("       -> pushed on STACK TOP (can be undone)\n");
}

/*=============================================================================
 *  FEATURE 2 : RECEIVE / DELIVER MESSAGE  (dequeue - FIFO)
 *===========================================================================*/
static void deliverMessage(void)
{
    ChatNode *m;
    char      ts[32];

    heading("RECEIVE / DELIVER MESSAGE");

    if (qFront == NULL) {                      /* queue underflow check      */
        printf("  [!] Queue is empty - there are no pending messages to deliver.\n");
        return;
    }

    m = dequeue();                             /* FIFO : oldest goes first   */
    m->status = DELIVERED;
    cDelivered++;

    printf("  Delivering the message at the FRONT of the queue...\n\n");
    printf("     >>> \"%s\"\n", m->text);
    printf("         from   : %s\n", m->sender);
    printf("         sent at: %s\n", fmtTime(m->stamp, ts, sizeof ts));
    printf("         msg id : #%03d\n", m->id);
    printf("\n  [OK] Status changed PENDING -> DELIVERED.  %d message(s) still pending.\n",
           nPending);

    if (qFront != NULL)
        printf("       Next in line at FRONT: \"%s\"\n", qFront->ref->text);
}

/*=============================================================================
 *  FEATURE 3 : VIEW CHAT HISTORY  (linked list traversal)
 *===========================================================================*/
static void viewHistory(void)
{
    ChatNode *cur;
    int       n = 0;

    heading("CHAT HISTORY  (Linked List Traversal)");

    if (histHead == NULL) {
        printf("  [!] No messages yet. Chat history is empty.\n");
        return;
    }

    printRowHeader();
    for (cur = histHead; cur != NULL; cur = cur->next) { printRow(cur); n++; }
    printf("  ");
    line_('-', 60);
    printf("  Total messages in history: %d   (Delivered: %d, Pending: %d)\n",
           n, n - nPending, nPending);
}

/*=============================================================================
 *  FEATURE 4 : SEARCH MESSAGE  (linear search over the linked list)
 *===========================================================================*/
static void searchMessage(void)
{
    char      key[MAX_INPUT];
    ChatNode *cur;
    int       found = 0;

    heading("SEARCH MESSAGE");

    if (histHead == NULL) {
        printf("  [!] Chat history is empty - nothing to search.\n");
        return;
    }

    printf("  Search (keyword or sender name): ");
    readLine(key, sizeof key);
    trim(key);

    if (key[0] == '\0') {
        printf("\n  [!] Empty keyword. Search cancelled.\n");
        return;
    }

    printf("\n  Searching for \"%s\" ...\n\n", key);
    for (cur = histHead; cur != NULL; cur = cur->next) {
        if (ciFind(cur->text, key) >= 0 || ciFind(cur->sender, key) >= 0) {
            if (!found) printRowHeader();
            printRow(cur);
            found++;
        }
    }

    if (found == 0)
        printf("  [X] No message matching \"%s\" was found.\n", key);
    else {
        printf("  ");
        line_('-', 60);
        printf("  Message Found: %d match(es) for \"%s\".\n", found, key);
    }
}

/*=============================================================================
 *  FEATURE 5 : DELETE LAST MESSAGE  (stack pop - LIFO undo)
 *  The popped message must also be pulled out of the queue (if it was still
 *  pending) and out of the linked list, and only then freed.
 *===========================================================================*/
static void deleteLastMessage(void)
{
    ChatNode *m;
    char      ts[32];
    int       wasPending;

    heading("DELETE LAST MESSAGE  (Undo - LIFO)");

    if (sTop == NULL) {                        /* stack underflow check      */
        printf("  [!] Stack is empty - there is no message left to delete.\n");
        return;
    }

    m          = pop();                        /* newest message comes out   */
    wasPending = (m->status == PENDING);

    if (wasPending) queueUnlinkRef(m);         /* pull it out of the queue   */
    listUnlink(m);                             /* pull it out of the history */

    printf("  Removing the message on TOP of the stack...\n\n");
    printf("     [DELETED] #%03d  \"%s\"\n", m->id, m->text);
    printf("               sent by %s at %s\n", m->sender, fmtTime(m->stamp, ts, sizeof ts));
    printf("               it was %s\n", wasPending ? "still PENDING (also removed from the queue)"
                                                    : "already DELIVERED");

    free(m);                                   /* single owner frees it      */
    cDeleted++;

    if (peek() != NULL) printf("\n  TOP of stack is now -> \"%s\"\n", peek()->text);
    else                printf("\n  Stack is now empty - nothing more to undo.\n");
}

/*=============================================================================
 *  FEATURE 6 : VIEW PENDING MESSAGES  (queue traversal, FRONT -> REAR)
 *===========================================================================*/
static void viewPending(void)
{
    QNode *cur;
    int    pos = 1;
    char   ts[32];

    heading("PENDING MESSAGES  (Queue : FIFO, Front -> Rear)");

    if (qFront == NULL) {
        printf("  [!] Queue is empty - every message has been delivered.\n");
        return;
    }

    printf("    FRONT  (delivered first)\n");
    printf("      |\n");
    printf("      v\n");
    for (cur = qFront; cur != NULL; cur = cur->next) {
        fmtTime(cur->ref->stamp, ts, sizeof ts);
        printf("    %2d. \"%s\"\n", pos, cur->ref->text);
        printf("        [#%03d  %s  %s]\n", cur->ref->id, cur->ref->sender, ts);
        pos++;
    }
    printf("      ^\n");
    printf("      |\n");
    printf("    REAR   (delivered last)\n\n");
    printf("  %d message(s) waiting for delivery.\n", nPending);
}

/*=============================================================================
 *  FEATURE 7 : CLEAR CHAT  (free every node of all three structures)
 *===========================================================================*/
static void freeEverything(void)
{
    ChatNode *c, *cn;
    QNode    *q, *qn;
    SNode    *s, *sn;

    for (q = qFront; q != NULL; q = qn) { qn = q->next; free(q); }
    qFront = qRear = NULL;

    for (s = sTop;   s != NULL; s = sn) { sn = s->next; free(s); }
    sTop = NULL;

    /* the linked list is the owner of the message data, so it is freed last */
    for (c = histHead; c != NULL; c = cn) { cn = c->next; free(c); }
    histHead = histTail = NULL;

    nHistory = nPending = 0;
}

static void clearChat(void)
{
    char ans[MAX_INPUT];
    int  removed;

    heading("CLEAR CHAT");

    if (histHead == NULL) {
        printf("  [!] Chat is already empty.\n");
        return;
    }

    printf("  This will permanently erase %d message(s) from the history,\n", nHistory);
    printf("  the pending queue and the undo stack.\n");
    printf("  Are you sure? (y/n): ");
    readLine(ans, sizeof ans);
    trim(ans);

    if (ans[0] != 'y' && ans[0] != 'Y') {
        printf("\n  Cancelled. Nothing was deleted.\n");
        return;
    }

    removed   = nHistory;
    cDeleted += removed;
    freeEverything();
    printf("\n  [OK] %d message(s) erased. Linked list, queue and stack are now empty.\n",
           removed);
}

/*=============================================================================
 *  MAIN - menu driven loop
 *===========================================================================*/
int main(void)
{
    char name[MAX_INPUT];
    int  choice, running = 1;

    line_('=', 62);
    printf("        CHAT MESSAGE MANAGEMENT SYSTEM\n");
    printf("        using Queue, Stack and Linked List\n");
    line_('=', 62);
    printf("  Enter your name: ");
    readLine(name, sizeof name);
    trim(name);
    if (name[0] != '\0') {
        strncpy(currentUser, name, MAX_NAME - 1);
        currentUser[MAX_NAME - 1] = '\0';
    }
    printf("\n  Welcome, %s!\n", currentUser);

    while (running) {
        showMenu();
        choice = readChoice();

        switch (choice) {
            case 1: sendMessage();       pause_(); break;
            case 2: deliverMessage();    pause_(); break;
            case 3: viewHistory();       pause_(); break;
            case 4: searchMessage();     pause_(); break;
            case 5: deleteLastMessage(); pause_(); break;
            case 6: viewPending();       pause_(); break;
            case 7: clearChat();         pause_(); break;
            case 8:
                heading("SESSION SUMMARY");
                statusBar();
                freeEverything();          /* release all heap memory        */
                printf("\n  All memory freed. Goodbye, %s!\n\n", currentUser);
                running = 0;
                break;
            default:
                printf("\n  [!] Invalid choice. Please enter a number from 1 to 8.\n");
                pause_();
                break;
        }
    }
    return 0;
}
