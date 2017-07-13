# Comment/uncomment the flag below to disable/enable debug msg
DEBUG = y

# Add debug flags to CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g -dSCULL_DEBUG # -O needed for expand inlines
else
  DBFLAGS = -O2
endif

CFLAGS += $(DEBFLAGS)

obj-m += hello.o scull.o pscull.o
all:
	make -C ~/kernel M=$(PWD) modules CFLAGS='$(CFLAGS)'
clean:
	make -C ~/kernel M=$(PWD) clean
