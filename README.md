# Chat Management System CLI

A simple command-line application for creating chats, managing message histories, and searching messages. This Python mini-project demonstrates common data structures and saves chat data locally in JSON format.

## Features

- Create and delete chats
- List chats with their message counts
- Send messages with a sender name and timestamp
- View a chat’s message history
- Search messages across chats, without case sensitivity
- Save and load chats using a local JSON file
- Process pending messages in first-in, first-out order

## Data Structures

| Data structure | Use |
|---|---|
| Dictionary | Stores chats by ID for quick lookup |
| Deque | Holds pending messages in FIFO order |
| List | Stores messages in chronological order |

Chat lookup is typically **O(1)**. Searching checks each stored message, so it takes **O(M)** message checks, where *M* is the total number of messages.

## Requirements

- Python 3.10 or newer
- No third-party packages

## Run the Program

Clone the repository, then open a terminal in the project folder and run:

```bash
python main.py
```

On Windows, you can also run:

```bash
py main.py
```

## Menu Options

```text
1. Create chat
2. List chats
3. Send message
4. View messages
5. Search messages
6. Delete chat
7. Process pending queue
0. Exit
```

Create a chat first. Use its displayed ID when sending or viewing messages. Choose **0** to save and exit.

## Project Structure

```text
chat-management-system/
├── main.py       # Application and command-line menu
├── README.md     # Project documentation
└── chats.json    # Created automatically to store chats
```

## Storage

The application creates `chats.json` beside `main.py` when it saves chat data. Keep this file if you want to retain your chats between runs.

## Limitations

This project is a local command-line demonstration. It does not send messages over a network, support multiple accounts, or encrypt stored messages. Pending messages are kept in memory until processed.

## Future Improvements

- Add user accounts and contact management
- Support group conversations
- Persist pending messages
- Add message editing and deletion
- Use a database for larger chat histories
- Add network-based messaging

## License

Add a license before distributing or reusing this project publicly.