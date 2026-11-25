// A small log of what the background work did, in kaiplayer.log in
// KaiPlayer's folder: each line is written and closed at once, so a freeze
// leaves its last step there. The log of the run before is kept as
// kaiplayer.old.log. Any thread may write.
#pragma once

void appLogStart(void);
// printf-style; a line break is added.
void appLog(const char *format, ...) __attribute__((format(printf, 1, 2)));
