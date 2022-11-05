CC := clang
CFLAGS := -g -Wall -Werror

all: mysh

clean:
	@echo "Removing build output"
	@rm -rf mysh mysh.zip mysh.dSYM

mysh: mysh.c
	$(CC) $(CFLAGS) -o mysh mysh.c

zip:
	@echo "Generating mysh.zip file to submit to Gradescope..."
	@zip -q -r mysh.zip . -x .git/\* .vscode/\* .clang-format .gitignore mysh
	@echo "Done. Please upload mysh.zip to Gradescope."

format:
	@echo "Reformatting source code."
	@clang-format -i --style=file $(wildcard *.c) $(wildcard *.h)
	@echo "Done."

.PHONY: all clean zip format
