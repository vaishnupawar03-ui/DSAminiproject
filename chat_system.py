#!/usr/bin/env python3
"""
===============================================================================
 PROJECT : Chat Message Management System Using Queue, Stack and Linked List
 LANGUAGE: Python 3 (standard library only - no external packages)
 RUN     : python3 chat_system.py
-------------------------------------------------------------------------------
 DATA STRUCTURES USED
 ------------------------------------------------------------------------------
  LINKED LIST : permanent chat history. Grows dynamically, one node per
                message. head/tail references give O(1) append.
  QUEUE       : pending (undelivered) messages. FIFO - the message sent
                first is the message delivered first.
  STACK       : undo buffer. LIFO - "Delete Last Message" always removes
                the most recently sent message first.

 IMPORTANT - these structures are BUILT BY HAND from node classes.
 Python's list, collections.deque, queue.Queue and str.find are deliberately
 NOT used for the data structures, because the point of the project is to
 implement the structures themselves.

 MEMORY MODEL (read before modifying)
 ------------------------------------------------------------------------------
  Every message is stored EXACTLY ONCE, inside a ChatNode of the linked list.
  The Queue and the Stack do NOT copy the message; they only hold a REFERENCE
  to that ChatNode. This means:
      - no duplicated text, no risk of the three views going out of sync
      - marking a message DELIVERED updates one object, seen by all three
  Python frees memory automatically, so unlike the C version there is no
  free(). A node disappears once the LAST reference to it is dropped - which
  is why deleting a message must unlink it from the queue AND the list, not
  just pop it off the stack.
===============================================================================
"""

import time

# ----------------------------------------------------------------- constants
MAX_TEXT = 200          # maximum characters stored per message
MAX_NAME = 30           # maximum characters stored per user name

PENDING = 0
DELIVERED = 1


# =============================================================================
#  NODE CLASSES
# =============================================================================
class ChatNode:
    """A single message. This object OWNS the message data.

    It is a node of the linked list (chat history); the queue and the stack
    only ever hold references to it.
    """

    __slots__ = ("id", "sender", "text", "stamp", "status", "next")

    def __init__(self, msg_id: int, sender: str, text: str) -> None:
        self.id = msg_id
        self.sender = sender
        self.text = text
        self.stamp = time.time()        # timestamp, seconds since epoch
        self.status = PENDING
        self.next = None                # link to the next message in history


class QNode:
    """Queue node - holds a reference to a ChatNode, not a copy."""

    __slots__ = ("ref", "next")

    def __init__(self, ref: ChatNode) -> None:
        self.ref = ref
        self.next = None


class SNode:
    """Stack node - holds a reference to a ChatNode, not a copy."""

    __slots__ = ("ref", "next")

    def __init__(self, ref: ChatNode) -> None:
        self.ref = ref
        self.next = None


# =============================================================================
#  SMALL UTILITIES
# =============================================================================
class InputClosed(Exception):
    """Raised when stdin reaches EOF (Ctrl+D / Ctrl+Z, or a piped file ends).

    Without this the menu loop would spin forever reading empty lines.
    """


def read_line(prompt: str = "") -> str:
    """Read one line, stripped of surrounding blanks. Raises InputClosed at EOF."""
    try:
        return input(prompt).strip()
    except EOFError:
        raise InputClosed from None


def read_choice(prompt: str = "") -> int:
    """Read a menu choice. Returns -1 if the user typed something non-numeric."""
    text = read_line(prompt)
    try:
        return int(text)
    except ValueError:
        return -1


def fmt_time(stamp: float) -> str:
    """Format a timestamp as '28-Sep-2026 14:32:01'."""
    return time.strftime("%d-%b-%Y %H:%M:%S", time.localtime(stamp))


def ci_find(haystack: str, needle: str) -> int:
    """Case-insensitive substring search, written out by hand.

    str.find() would do this in one call, but the project is about
    implementing the search itself. Returns the match index, or -1.
    """
    hl, nl = len(haystack), len(needle)
    if nl == 0 or nl > hl:
        return -1
    for i in range(hl - nl + 1):
        j = 0
        while j < nl and haystack[i + j].lower() == needle[j].lower():
            j += 1
        if j == nl:
            return i
    return -1


def line_(char: str, n: int) -> None:
    print(char * n)


def heading(title: str) -> None:
    print()
    line_("-", 62)
    print(f"  {title}")
    line_("-", 62)


# =============================================================================
#  THE CHAT SYSTEM
# =============================================================================
class ChatSystem:
    """Holds the linked list, the queue, the stack and the counters."""

    def __init__(self, user: str = "User") -> None:
        # 1. LINKED LIST -> chat history
        self.hist_head = None
        self.hist_tail = None
        # 2. QUEUE -> pending messages (FIFO)
        self.q_front = None
        self.q_rear = None
        # 3. STACK -> undo buffer (LIFO)
        self.s_top = None
        # counters (the "message counter" feature)
        self.c_sent = 0
        self.c_delivered = 0
        self.c_deleted = 0
        self.n_history = 0
        self.n_pending = 0
        self.next_id = 1
        self.user = user
        self.running = True

    # ------------------------------------------------------------------
    #  LINKED LIST OPERATIONS (chat history)
    # ------------------------------------------------------------------
    def list_append(self, node: ChatNode) -> None:
        """Append at the tail so history stays chronological.  O(1)"""
        node.next = None
        if self.hist_head is None:
            self.hist_head = self.hist_tail = node
        else:
            self.hist_tail.next = node
            self.hist_tail = node
        self.n_history += 1

    def list_unlink(self, target: ChatNode) -> bool:
        """Unlink one specific node from the history list.  O(n)"""
        cur, prev = self.hist_head, None
        while cur is not None and cur is not target:
            prev, cur = cur, cur.next
        if cur is None:
            return False
        if prev is None:
            self.hist_head = cur.next
        else:
            prev.next = cur.next
        if cur is self.hist_tail:
            self.hist_tail = prev
        cur.next = None
        self.n_history -= 1
        return True

    # ------------------------------------------------------------------
    #  QUEUE OPERATIONS (pending messages, FIFO)
    # ------------------------------------------------------------------
    def enqueue(self, node: ChatNode) -> None:
        """Insert at the REAR.  O(1)"""
        qn = QNode(node)
        if self.q_rear is None:
            self.q_front = self.q_rear = qn      # queue was empty
        else:
            self.q_rear.next = qn
            self.q_rear = qn
        self.n_pending += 1

    def dequeue(self):
        """Remove from the FRONT and return the message.  O(1)"""
        if self.q_front is None:
            return None                          # underflow
        qn = self.q_front
        self.q_front = qn.next
        if self.q_front is None:
            self.q_rear = None                   # queue became empty
        self.n_pending -= 1
        return qn.ref

    def queue_unlink_ref(self, target: ChatNode) -> bool:
        """Take a specific message out of the middle of the queue.  O(n)

        Needed when a message is undone before it was ever delivered.
        """
        cur, prev = self.q_front, None
        while cur is not None and cur.ref is not target:
            prev, cur = cur, cur.next
        if cur is None:
            return False
        if prev is None:
            self.q_front = cur.next
        else:
            prev.next = cur.next
        if cur is self.q_rear:
            self.q_rear = prev
        self.n_pending -= 1
        return True

    # ------------------------------------------------------------------
    #  STACK OPERATIONS (undo buffer, LIFO)
    # ------------------------------------------------------------------
    def push(self, node: ChatNode) -> None:
        """Push on TOP.  O(1)"""
        sn = SNode(node)
        sn.next = self.s_top
        self.s_top = sn

    def pop(self):
        """Pop from TOP and return the message.  O(1)"""
        if self.s_top is None:
            return None                          # underflow
        sn = self.s_top
        self.s_top = sn.next
        return sn.ref

    def peek(self):
        return None if self.s_top is None else self.s_top.ref

    # ------------------------------------------------------------------
    #  DISPLAY HELPERS
    # ------------------------------------------------------------------
    @staticmethod
    def print_row_header() -> None:
        print(f"  {'ID':<5} {'TIMESTAMP':<21} {'SENDER':<12} {'STATUS':<10} MESSAGE")
        print("  ", end="")
        line_("-", 60)

    @staticmethod
    def print_row(m: ChatNode) -> None:
        status = "DELIVERED" if m.status == DELIVERED else "PENDING"
        print(f"  {'#%03d' % m.id:<5} {fmt_time(m.stamp):<21} "
              f"{m.sender:<12} {status:<10} {m.text}")

    def status_bar(self) -> None:
        print(f"  Sent: {self.c_sent:<4} Delivered: {self.c_delivered:<4} "
              f"Pending: {self.n_pending:<4} History: {self.n_history:<4} "
              f"Deleted: {self.c_deleted:<4}")

    def show_menu(self) -> None:
        print()
        line_("=", 62)
        print("               C H A T   M E S S A G E   S Y S T E M")
        print(f"                     Logged in as : {self.user}")
        line_("=", 62)
        self.status_bar()
        line_("=", 62)
        print("   1. Send Message              5. Delete Last Message (Undo)")
        print("   2. Receive/Deliver Message   6. View Pending Messages")
        print("   3. View Chat History         7. Clear Chat")
        print("   4. Search Message            8. Exit")
        line_("=", 62)

    # ------------------------------------------------------------------
    #  FEATURE 1 : SEND MESSAGE
    #  One action touches all three structures.
    # ------------------------------------------------------------------
    def send_message(self) -> None:
        heading("SEND MESSAGE")
        print(f"  Type your message (max {MAX_TEXT} chars, blank to cancel)")
        text = read_line(f"  {self.user} > ")

        if not text:
            print("\n  [!] Empty message discarded. Nothing was sent.")
            return

        node = ChatNode(self.next_id, self.user[:MAX_NAME], text[:MAX_TEXT])
        self.next_id += 1

        self.list_append(node)      # 1. linked list : permanent history
        self.enqueue(node)          # 2. queue       : waiting for delivery
        self.push(node)             # 3. stack       : newest on top, undoable
        self.c_sent += 1

        print(f"\n  [OK] Message #{node.id:03d} sent at {fmt_time(node.stamp)}")
        print(f'       "{node.text}"')
        print("       -> added to HISTORY (linked list)")
        print(f"       -> added to QUEUE at REAR (position {self.n_pending}, awaiting delivery)")
        print("       -> pushed on STACK TOP (can be undone)")

    # ------------------------------------------------------------------
    #  FEATURE 2 : RECEIVE / DELIVER MESSAGE  (dequeue - FIFO)
    # ------------------------------------------------------------------
    def deliver_message(self) -> None:
        heading("RECEIVE / DELIVER MESSAGE")

        if self.q_front is None:                 # underflow check
            print("  [!] Queue is empty - there are no pending messages to deliver.")
            return

        m = self.dequeue()                       # FIFO : oldest goes first
        m.status = DELIVERED
        self.c_delivered += 1

        print("  Delivering the message at the FRONT of the queue...\n")
        print(f'     >>> "{m.text}"')
        print(f"         from   : {m.sender}")
        print(f"         sent at: {fmt_time(m.stamp)}")
        print(f"         msg id : #{m.id:03d}")
        print(f"\n  [OK] Status changed PENDING -> DELIVERED.  "
              f"{self.n_pending} message(s) still pending.")

        if self.q_front is not None:
            print(f'       Next in line at FRONT: "{self.q_front.ref.text}"')

    # ------------------------------------------------------------------
    #  FEATURE 3 : VIEW CHAT HISTORY  (linked list traversal)
    # ------------------------------------------------------------------
    def view_history(self) -> None:
        heading("CHAT HISTORY  (Linked List Traversal)")

        if self.hist_head is None:
            print("  [!] No messages yet. Chat history is empty.")
            return

        self.print_row_header()
        count = 0
        cur = self.hist_head
        while cur is not None:                   # traversal
            self.print_row(cur)
            count += 1
            cur = cur.next
        print("  ", end="")
        line_("-", 60)
        print(f"  Total messages in history: {count}   "
              f"(Delivered: {count - self.n_pending}, Pending: {self.n_pending})")

    # ------------------------------------------------------------------
    #  FEATURE 4 : SEARCH MESSAGE  (linear search over the linked list)
    # ------------------------------------------------------------------
    def search_message(self) -> None:
        heading("SEARCH MESSAGE")

        if self.hist_head is None:
            print("  [!] Chat history is empty - nothing to search.")
            return

        key = read_line("  Search (keyword or sender name): ")
        if not key:
            print("\n  [!] Empty keyword. Search cancelled.")
            return

        print(f'\n  Searching for "{key}" ...\n')
        found = 0
        cur = self.hist_head
        while cur is not None:
            if ci_find(cur.text, key) >= 0 or ci_find(cur.sender, key) >= 0:
                if found == 0:
                    self.print_row_header()
                self.print_row(cur)
                found += 1
            cur = cur.next

        if found == 0:
            print(f'  [X] No message matching "{key}" was found.')
        else:
            print("  ", end="")
            line_("-", 60)
            print(f'  Message Found: {found} match(es) for "{key}".')

    # ------------------------------------------------------------------
    #  FEATURE 5 : DELETE LAST MESSAGE  (stack pop - LIFO undo)
    #  Must keep all three structures consistent.
    # ------------------------------------------------------------------
    def delete_last_message(self) -> None:
        heading("DELETE LAST MESSAGE  (Undo - LIFO)")

        if self.s_top is None:                   # underflow check
            print("  [!] Stack is empty - there is no message left to delete.")
            return

        m = self.pop()                           # newest message comes out
        was_pending = (m.status == PENDING)

        if was_pending:
            self.queue_unlink_ref(m)             # pull it out of the queue
        self.list_unlink(m)                      # pull it out of the history

        print("  Removing the message on TOP of the stack...\n")
        print(f'     [DELETED] #{m.id:03d}  "{m.text}"')
        print(f"               sent by {m.sender} at {fmt_time(m.stamp)}")
        print("               it was " + ("still PENDING (also removed from the queue)"
                                          if was_pending else "already DELIVERED"))
        self.c_deleted += 1
        # Every reference is now gone, so Python's garbage collector reclaims
        # the node automatically - this is the equivalent of free() in C.
        del m

        top = self.peek()
        if top is not None:
            print(f'\n  TOP of stack is now -> "{top.text}"')
        else:
            print("\n  Stack is now empty - nothing more to undo.")

    # ------------------------------------------------------------------
    #  FEATURE 6 : VIEW PENDING MESSAGES  (queue traversal, FRONT -> REAR)
    # ------------------------------------------------------------------
    def view_pending(self) -> None:
        heading("PENDING MESSAGES  (Queue : FIFO, Front -> Rear)")

        if self.q_front is None:
            print("  [!] Queue is empty - every message has been delivered.")
            return

        print("    FRONT  (delivered first)")
        print("      |")
        print("      v")
        pos = 1
        cur = self.q_front
        while cur is not None:                   # traversal
            print(f'    {pos:2d}. "{cur.ref.text}"')
            print(f"        [#{cur.ref.id:03d}  {cur.ref.sender}  {fmt_time(cur.ref.stamp)}]")
            pos += 1
            cur = cur.next
        print("      ^")
        print("      |")
        print("    REAR   (delivered last)\n")
        print(f"  {self.n_pending} message(s) waiting for delivery.")

    # ------------------------------------------------------------------
    #  FEATURE 7 : CLEAR CHAT
    # ------------------------------------------------------------------
    def clear_everything(self) -> None:
        """Drop all three structures. Python reclaims the nodes."""
        self.q_front = self.q_rear = None
        self.s_top = None
        self.hist_head = self.hist_tail = None
        self.n_history = 0
        self.n_pending = 0

    def clear_chat(self) -> None:
        heading("CLEAR CHAT")

        if self.hist_head is None:
            print("  [!] Chat is already empty.")
            return

        print(f"  This will permanently erase {self.n_history} message(s) from the history,")
        print("  the pending queue and the undo stack.")
        ans = read_line("  Are you sure? (y/n): ")

        if not ans or ans[0] not in ("y", "Y"):
            print("\n  Cancelled. Nothing was deleted.")
            return

        removed = self.n_history
        self.c_deleted += removed
        self.clear_everything()
        print(f"\n  [OK] {removed} message(s) erased. "
              "Linked list, queue and stack are now empty.")

    # ------------------------------------------------------------------
    #  FEATURE 8 : EXIT
    # ------------------------------------------------------------------
    def exit_program(self) -> None:
        heading("SESSION SUMMARY")
        self.status_bar()
        self.clear_everything()
        print(f"\n  All memory released. Goodbye, {self.user}!\n")
        self.running = False


# =============================================================================
#  MAIN - menu driven loop
# =============================================================================
def main() -> None:
    line_("=", 62)
    print("        CHAT MESSAGE MANAGEMENT SYSTEM")
    print("        using Queue, Stack and Linked List")
    line_("=", 62)

    try:
        name = read_line("  Enter your name: ")
    except InputClosed:
        print("\n  [i] No input available - exiting.")
        return

    chat = ChatSystem(name[:MAX_NAME] if name else "User")
    print(f"\n  Welcome, {chat.user}!")

    actions = {
        1: chat.send_message,
        2: chat.deliver_message,
        3: chat.view_history,
        4: chat.search_message,
        5: chat.delete_last_message,
        6: chat.view_pending,
        7: chat.clear_chat,
        8: chat.exit_program,
    }

    while chat.running:
        try:
            chat.show_menu()
            choice = read_choice("  Enter choice: ")

            if choice in actions:
                actions[choice]()
            else:
                print("\n  [!] Invalid choice. Please enter a number from 1 to 8.")

            if chat.running:
                read_line("\n  Press ENTER to return to the menu...")

        except InputClosed:
            # stdin closed (Ctrl+D / Ctrl+Z / end of a piped file): a valid
            # choice can never arrive again, so exit cleanly instead of
            # looping forever on "Invalid choice".
            print("\n\n  [i] Input stream closed - exiting.")
            chat.exit_program()
            break


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\n  [i] Interrupted by user. Goodbye!\n")
