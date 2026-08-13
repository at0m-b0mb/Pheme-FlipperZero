# The protocol and the grading engine, on the host, under ASan and UBSan.
# No Flipper, no radio, no GUI - the same .c files that ship in the .fap.
#
#   make -C test

CC     ?= cc
CFLAGS := -std=gnu2x -Wall -Wextra -Werror -O1 -g
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS += -I ..

POCSAG_SRC  := host_pocsag_test.c ../helpers/phm_pocsag.c ../helpers/phm_bch.c \
               ../helpers/phm_gen.c
PRIVACY_SRC := host_privacy_test.c ../helpers/phm_privacy.c ../helpers/phm_pocsag.c \
               ../helpers/phm_bch.c ../helpers/phm_gen.c
ROSTER_SRC  := host_roster_test.c ../helpers/phm_roster.c ../helpers/phm_privacy.c \
               ../helpers/phm_pocsag.c ../helpers/phm_bch.c ../helpers/phm_gen.c

MOCKUP_SRC  := host_mockup_dump.c ../helpers/phm_privacy.c ../helpers/phm_pocsag.c \
               ../helpers/phm_bch.c ../helpers/phm_gen.c ../helpers/phm_roster.c

BINS := host_pocsag_test host_privacy_test host_roster_test

.PHONY: all run clean mockup
all: run

host_pocsag_test: $(POCSAG_SRC) phm_test.h ../helpers/phm_pocsag.h ../helpers/phm_bch.h
	$(CC) $(CFLAGS) $(POCSAG_SRC) -o $@

host_privacy_test: $(PRIVACY_SRC) phm_test.h ../helpers/phm_privacy.h
	$(CC) $(CFLAGS) $(PRIVACY_SRC) -o $@

host_roster_test: $(ROSTER_SRC) phm_test.h ../helpers/phm_roster.h
	$(CC) $(CFLAGS) $(ROSTER_SRC) -o $@

# Feeds tools_gen_mockups.py: runs the real decoder over the real demo channel
# and prints what the screens will show. Built without the sanitizers so the
# output is clean text, and kept out of `run` because it asserts nothing.
host_mockup_dump: $(MOCKUP_SRC)
	$(CC) -std=gnu2x -Wall -Wextra -Werror -O1 -I .. $(MOCKUP_SRC) -o $@

mockup: host_mockup_dump
	./host_mockup_dump > mockup_data.txt
	@wc -l mockup_data.txt

run: $(BINS)
	./host_pocsag_test
	@echo
	./host_privacy_test
	@echo
	./host_roster_test

clean:
	rm -rf $(BINS) host_mockup_dump mockup_data.txt *.dSYM
