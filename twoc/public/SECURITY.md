# Security

If you find a security problem in Project Wrath (for example, a crafted disc image, settings file or save that makes
the program run code it shouldn't, or a way it writes outside its own folders), please don't post the details in a
public issue: report it privately through the repository's "Report a vulnerability" button (GitHub's private
vulnerability reporting) and say how to reproduce it. The program makes no network connections and sends nothing by
itself; it reads the player's own disc image, its settings file and its saves, and writes only its game files,
saves, settings and logs. Fixes come with the next release, which credits the reporter if they wish.
