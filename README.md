# POSIXMQCONTROL(1)       FreeBSD General Commands Manual      POSIXMQCONTROL(1)

# NAME
     posixmqcontrol – Control POSIX mqueuefs message queues

# SYNOPSIS
     posixmqcontrol create -q queue -s size -d depth [-m mode] [-g group]
                    [-u user]
     posixmqcontrol info -q queue
     posixmqcontrol recv -q queue
     posixmqcontrol rm -q queue
     posixmqcontrol send -q queue -c content [-p priority]

# DESCRIPTION
     The posixmqcontrol command manipulates the named POSIX message queue. It
     allows creating queues, inspecting queue metadata, altering group and
     user access to queues, dumping queue contents, and unlinking queues.

     Unlinking removes the name from the system and frees underlying memory.

     The maximum message size, maximum queue size, and current queue depth are
     displayed by the info subcommand. This output is similar to running cat
     on a mqueuefs queue mounted under a mount point. This utility requires
     mqueuefs kernal module to be loaded but does not require mqueuefs to be
     mounted as a file system.

     The following subcommands are provided:

     create    Create the named queues, if they do not already exist. More
               than one queue name may be created. The same depth and size are
               used to create all queues. If a queue exists, then the depth
               are size are optional.

               The required size and depth arguments specify the maxmimum
               message size and maxmimum message depth.  The optional
               numerical mode argument specifies the initial access mode. If
               the queue exists but does not match the requested size and
               depth, this utility will attempt to recreate the queue by first
               unlinking and then creating it. This will fail if the queue is
               not empty or is opened by other processes.

     rm        Unlink the queues specified - one attempt per queue. Failure to
               unlink one queue does not stop this sub-command from attempting
               to unlink the others.

     info      For each named queue, dispay the maximum message size, maximum
               queue size, current queue depth, user owner id, group owner id,
               and mode permission bits.

     recv      Wait for a message from a single named queue and display the
               message to standard output.

     send      Send messages to one or more named queues. If multiple messages
               and multiple queues are specified, the utility attempts to send
               all messages to all queues.  The optional -p priority, if
               omitted, defaults to MQ_PRIO_MAX / 2 or medium priority.

# SUMMARY
     posixmqcontrol allows you to move POSIX message queue administration out
     of your applications. Defining and adjusting queue attributes can be done
     without touching application code. To avoid down-time when altering queue
     attributes, consider creating a new queue and configure reading
     applications to drain both new and old queues. Retire the old queue once
     all writers have been updated to write to the new queue.

# EXIT STATUS
     The posixmqcontrol utility exits 0 on success, and >0 if an error occurs.
     An exit value of 78 (ENOSYS) usually means the mqueuefs kernel module is
     not loaded.

# EXAMPLES
     •   To retrieve the current message from a named queue, /1, use the
         command
               posixmqcontrol recv -q /1

     •   To create a queue with the name /2 with message size 100 and maximum
         queue depth 10, use the command
               posixmqcontrol create -q /2 -s 100 -d 10

     •   To send a message to a queue with the name /3 use the command
               posixmqcontrol send -q /3 -c 'some choice words.'

     •   To examine attributes of a queue named /4 use the command
               posixmqcontrol info -q /4

# SEE ALSO
     mq_open(2), mq_getattr(2), mq_receive(2), mq_send(2), mq_setattr(2),
     mq_unlink(2), mqueuefs(5)

# BUGS
     mq_timedsend and mq_timedrecv are not implemented.  info reports a worst-
     case estimate for QSIZE.

# AUTHORS
     The posixmqcontrol command and this manual page were written by Rick
     Parrish <unitrunker@unitrunker.net>

FreeBSD 14.0-RELEASE-p3        February 7, 2024        FreeBSD 14.0-RELEASE-p3
