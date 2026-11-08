---
name: readlist
description: Add papers to, or read and reorder, Eric's arXiv reading list (the readlist app). Use when asked to "add this paper to my reading list", "queue this arXiv paper", "what's on my reading list", "mark X as read", "move X to the top", or when a conversation surfaces a paper worth reading later.
---

# readlist

The reading list is a Markdown note managed by the `readlist` CLI. Always go
through the CLI: it fetches the real title, authors, and abstract from arXiv,
refuses duplicates, and writes the note atomically. Never invent a title or an
arXiv id; pass the link or id you were given.

```sh
readlist add 2204.07137 https://arxiv.org/abs/2311.07222   # newest go to the top
readlist add <link> --bottom                               # or to the bottom
readlist list --json                                       # queue, in priority order
readlist list --all --json                                 # queue plus read papers
readlist show <paper> --json                               # includes the abstract
readlist read <paper>          readlist unread <paper>
readlist move <paper> top|bottom|up|down|<position>
readlist remove <paper>
readlist path                                              # the note's location
```

`<paper>` is an arXiv id, a link, or a queue position (1 is the top). Commands
exit 0 on success and 1 with the reason on stderr (for example "already in the
queue"), so report that reason rather than retrying. Non-arXiv links work too:
the title comes from the page's meta tags.

Eric opens the list with the "Reading List" app from the Omarchy launcher. After
adding, tell him the title that was added and its position.
