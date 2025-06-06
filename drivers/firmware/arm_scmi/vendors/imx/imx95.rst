.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

===============================================================================
i.MX95 System Control and Management Interface(SCMI) Vendor Protocols Extension
===============================================================================

:Copyright: |copy| 2024 NXP

:Author: Peng Fan <peng.fan@nxp.com>

The System Manager (SM) is a low-level system function which runs on a System
Control Processor (SCP) to support isolation and management of power domains,
clocks, resets, sensors, pins, etc. on complex application processors. It often
runs on a Cortex-M processor and provides an abstraction to many of the
underlying features of the hardware. The primary purpose of the SM is to allow
isolation between software running on different cores in the SoC. It does this
by having exclusive access to critical resources such as those controlling
power, clocks, reset, PMIC, etc. and then providing an RPC interface to those
clients. This allows the SM to provide access control, arbitration, and
aggregation policies for those shared critical resources.

SM introduces a concept Logic Machine(LM) which is analogous to VM and each has
its own instance of SCMI. All normal SCMI calls only apply to that LM. That
includes boot, shutdown, reset, suspend, wake, etc. Each LM (e.g. A55 and M7)
are completely isolated from the others and each LM has its own communication
channels talking to the same SCMI server.

This document covers all the information necessary to understand, maintain,
port, and deploy the SM on supported processors.

The SM implements an interface compliant with the Arm SCMI Specification
with additional vendor specific extensions.

System Control and Management Logical Machine Management Vendor Protocol
========================================================================

The SM adds the concept of logical machines (LMs). These are analogous to
VMs and each has its own instance of SCMI. All normal SCMI calls only apply
the LM running the calling agent. That includes boot, shutdown, reset,
suspend, wake, etc. If a caller makes the SCMI base call to get a list
of agents, it will only get those on that LM. Each LM is completely isolated
from the others. This is mandatory for these to operate independently.

This protocol is intended to support boot, shutdown, and reset of other logical
machines (LM). It is usually used to allow one LM(e.g. OSPM) to manage
another LM which is usually an offload or accelerator engine. Notifications
from this protocol can also be used to manage a communication link to another
LM. The LMM protocol provides commands to:

- Describe the protocol version.
- Discover implementation attributes.
- Discover all the LMs defined in the system.
- Boot a target LM.
- Shutdown a target LM (gracefully or forcibly).
- Reset a target LM (gracefully or forcibly).
- Wake a target LM from suspend.
- Suspend a target LM (gracefully).
- Read boot/shutdown/reset information for a target LM.
- Get notifications when a target LM boots or shuts down (e.g. LM 'X' requested
  notification of LM 'Y' boots or shuts down, when LM 'Y' boots or shuts down,
  SCMI firmware will send notification to LM 'X').

'Graceful' means asking LM itself to shutdown/reset/etc (e.g. sending
notification to Linux, Then Linux reboots or powers down itself). It is async
command that the SUCCESS of the command just means the command successfully
return, not means reboot/reset successfully finished.

'Forceful' means the SM will force shutdown/reset/etc the LM. It is sync
command that the SUCCESS of the command means the LM has been successfully
shutdown/reset/etc.
If the commands not have Graceful/Forceful flag settings, such as WAKE, SUSEND,
it is a Graceful command.

Commands:
_________

PROTOCOL_VERSION
~~~~~~~~~~~~~~~~

message_id: 0x0
protocol_id: 0x80
This command is mandatory.

+---------------+--------------------------------------------------------------+
|Return values                                                                 |
+---------------+--------------------------------------------------------------+
|Name           |Description                                                   |
+---------------+--------------------------------------------------------------+
|int32 status   | See ARM SCMI Specification for status code definitions.      |
+---------------+--------------------------------------------------------------+
|uint32 version | For this revision of the specification, this value must be   |
|               | 0x10000.                                                     |
+---------------+--------------------------------------------------------------+

PROTOCOL_ATTRIBUTES
~~~~~~~~~~~~~~~~~~~

message_id: 0x1
protocol_id: 0x80
This command is mandatory.

+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      | See ARM SCMI Specification for status code definitions.   |
+------------------+-----------------------------------------------------------+
|uint32 attributes |Protocol attributes:                                       |
|                  |Bits[31:5] Reserved, must be zero.                         |
|                  |Bits[4:0] Number of Logical Machines                       |
|                  |Note that due to both hardware limitations and reset reason|
|                  |field limitations, the max number of LM is 16. The minimum |
|                  |is 1.                                                      |
+------------------+-----------------------------------------------------------+

PROTOCOL_MESSAGE_ATTRIBUTES
~~~~~~~~~~~~~~~~~~~~~~~~~~~

message_id: 0x2
protocol_id: 0x80
This command is mandatory.

+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: in case the message is implemented and available  |
|                  |to use.                                                    |
|                  |NOT_FOUND: if the message identified by message_id is      |
|                  |invalid or not implemented                                 |
+------------------+-----------------------------------------------------------+
|uint32 attributes |Flags that are associated with a specific command in the   |
|                  |protocol. For all commands in this protocol, this          |
|                  |parameter has a value of 0                                 |
+------------------+-----------------------------------------------------------+

LMM_ATTRIBUTES
~~~~~~~~~~~~~~

message_id: 0x3
protocol_id: 0x80
This command is mandatory.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 lmid       |ID of the Logical Machine                                  |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if valid attributes are returned.                 |
|                  |NOT_FOUND: if lmid not points to a valid logical machine.  |
|                  |DENIED: if the agent does not have permission to get info  |
|                  |for the LM specified by lmid.                              |
+------------------+-----------------------------------------------------------+
|uint32 lmid       |Identifier of the LM whose identification is requested.    |
|                  |This field is: Populated with the lmid of the calling      |
|                  |agent, when the lmid parameter passed via the command is   |
|                  |0xFFFFFFFF. Identical to the lmid field passed via the     |
|                  |calling parameters, in all other cases                     |
+------------------+-----------------------------------------------------------+
|uint32 attributes | Bits[31:0] reserved. must be zero                         |
+------------------+-----------------------------------------------------------+
|uint32 state      | Current state of the LM                                   |
+------------------+-----------------------------------------------------------+
|uint32 errStatus  | Last error status recorded                                |
+------------------+-----------------------------------------------------------+
|char name[16]     | A NULL terminated ASCII string with the LM name, of up    |
|                  | to 16 bytes                                               |
+------------------+-----------------------------------------------------------+

LMM_BOOT
~~~~~~~~

message_id: 0x4
protocol_id: 0x80
This command is mandatory.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 lmid       |ID of the Logical Machine                                  |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if LM boots successfully started.                 |
|                  |NOT_FOUND: if lmid not points to a valid logical machine.  |
|                  |INVALID_PARAMETERS: if lmid is same as the caller.         |
|                  |DENIED: if the agent does not have permission to manage the|
|                  |the LM specified by lmid.                                  |
+------------------+-----------------------------------------------------------+

LMM_RESET
~~~~~~~~~

message_id: 0x5
protocol_id: 0x80
This command is mandatory.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 lmid       |ID of the Logical Machine                                  |
+------------------+-----------------------------------------------------------+
|uint32 flags      |Reset flags:                                               |
|                  |Bits[31:1] Reserved, must be zero.                         |
|                  |Bit[0] Graceful request:                                   |
|                  |Set to 1 if the request is a graceful request.             |
|                  |Set to 0 if the request is a forceful request.             |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: The LMM RESET command finished successfully in    |
|                  |graceful reset or LM successfully resets in forceful reset.|
|                  |NOT_FOUND: if lmid not points to a valid logical machine.  |
|                  |INVALID_PARAMETERS: if lmid is same as the caller.         |
|                  |DENIED: if the agent does not have permission to manage the|
|                  |the LM specified by lmid.                                  |
+------------------+-----------------------------------------------------------+

LMM_SHUTDOWN
~~~~~~~~~~~~

message_id: 0x6
protocol_id: 0x80
This command is mandatory.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 lmid       |ID of the Logical Machine                                  |
+------------------+-----------------------------------------------------------+
|uint32 flags      |Reset flags:                                               |
|                  |Bits[31:1] Reserved, must be zero.                         |
|                  |Bit[0] Graceful request:                                   |
|                  |Set to 1 if the request is a graceful request.             |
|                  |Set to 0 if the request is a forceful request.             |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: The LMM shutdown command finished successfully in |
|                  |graceful request or LM successfully shutdown in forceful   |
|                  |request.                                                   |
|                  |NOT_FOUND: if lmid not points to a valid logical machine.  |
|                  |INVALID_PARAMETERS: if lmid is same as the caller.         |
|                  |DENIED: if the agent does not have permission to manage the|
|                  |the LM specified by lmid.                                  |
+------------------+-----------------------------------------------------------+

LMM_WAKE
~~~~~~~~

message_id: 0x7
protocol_id: 0x80
This command is mandatory.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 lmid       |ID of the Logical Machine                                  |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if LM wake command successfully returns.          |
|                  |NOT_FOUND: if lmid not points to a valid logical machine.  |
|                  |INVALID_PARAMETERS: if lmid is same as the caller.         |
|                  |DENIED: if the agent does not have permission to manage the|
|                  |the LM specified by lmid.                                  |
+------------------+-----------------------------------------------------------+

LMM_SUSPEND
~~~~~~~~~~~

message_id: 0x8
protocol_id: 0x80
This command is mandatory.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 lmid       |ID of the Logical Machine                                  |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if LM suspend command successfully returns.       |
|                  |NOT_FOUND: if lmid not points to a valid logical machine.  |
|                  |INVALID_PARAMETERS: if lmid is same as the caller.         |
|                  |DENIED: if the agent does not have permission to manage the|
|                  |the LM specified by lmid.                                  |
+------------------+-----------------------------------------------------------+

LMM_NOTIFY
~~~~~~~~~~

message_id: 0x9
protocol_id: 0x80
This command is mandatory.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 lmid       |ID of the Logical Machine                                  |
+------------------+-----------------------------------------------------------+
|uint32 flags      |Notification flags:                                        |
|                  |Bits[31:3] Reserved, must be zero.                         |
|                  |Bit[3] Wake (resume) notification:                         |
|                  |Set to 1 to send notification.                             |
|                  |Set to 0 if no notification.                               |
|                  |Bit[2] Suspend (sleep) notification:                       |
|                  |Set to 1 to send notification.                             |
|                  |Set to 0 if no notification.                               |
|                  |Bit[1] Shutdown (off) notification:                        |
|                  |Set to 1 to send notification.                             |
|                  |Set to 0 if no notification.                               |
|                  |Bit[0] Boot (on) notification:                             |
|                  |Set to 1 to send notification.                             |
|                  |Set to 0 if no notification                                |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if the notification state successfully updated.   |
|                  |NOT_FOUND: if lmid not points to a valid logical machine.  |
|                  |INVALID_PARAMETERS: if input attributes flag specifies     |
|                  |unsupported or invalid configurations.                     |
|                  |DENIED: if the agent does not have permission to request   |
|                  |the notification.                                          |
+------------------+-----------------------------------------------------------+

LMM_RESET_REASON
~~~~~~~~~~~~~~~~

message_id: 0xA
protocol_id: 0x80
This command is mandatory.

This command is to return the reset reason that caused the last reset, such as
POR, WDOG, JTAG and etc.

+---------------------+--------------------------------------------------------+
|Parameters                                                                    |
+---------------------+--------------------------------------------------------+
|Name                 |Description                                             |
+---------------------+--------------------------------------------------------+
|uint32 lmid          |ID of the Logical Machine                               |
+---------------------+--------------------------------------------------------+
|Return values                                                                 |
+---------------------+--------------------------------------------------------+
|Name                 |Description                                             |
+---------------------+--------------------------------------------------------+
|int32 status         |SUCCESS: if the reset reason of the LM successfully     |
|                     |updated.                                                |
|                     |NOT_FOUND: if lmid not points to a valid logical machine|
|                     |DENIED: if the agent does not have permission to request|
|                     |the reset reason.                                       |
+---------------------+--------------------------------------------------------+
|uint32 bootflags     |Boot reason flags. This parameter has the format:       |
|                     |Bits[31] Valid.                                         |
|                     |Set to 1 if the entire reason is valid.                 |
|                     |Set to 0 if the entire reason is not valid.             |
|                     |Bits[30:29] Reserved, must be zero.                     |
|                     |Bit[28] Valid origin:                                   |
|                     |Set to 1 if the origin field is valid.                  |
|                     |Set to 0 if the origin field is not valid.              |
|                     |Bits[27:24] Origin.                                     |
|                     |Logical Machine(LM) ID that causes the BOOT of this LM  |
|                     |Bit[23] Valid err ID:                                   |
|                     |Set to 1 if the error ID field is valid.                |
|                     |Set to 0 if the error ID field is not valid.            |
|                     |Bits[22:8] Error ID(Agent ID of the system).            |
|                     |Bit[7:0] Reason(WDOG, POR, FCCU and etc):               |
|                     |See the SRESR register description in the System        |
|                     |Reset Controller (SRC) section in SoC reference mannual |
|                     |One reason maps to BIT(reason) in SRESR                 |
+---------------------+--------------------------------------------------------+
|uint32 shutdownflags |Shutdown reason flags. This parameter has the format:   |
|                     |Bits[31] Valid.                                         |
|                     |Set to 1 if the entire reason is valid.                 |
|                     |Set to 0 if the entire reason is not valid.             |
|                     |Bits[30:29] Number of valid extended info words.        |
|                     |Bit[28] Valid origin:                                   |
|                     |Set to 1 if the origin field is valid.                  |
|                     |Set to 0 if the origin field is not valid.              |
|                     |Bits[27:24] Origin.                                     |
|                     |Logical Machine(LM) ID that causes the BOOT of this LM  |
|                     |Bit[23] Valid err ID:                                   |
|                     |Set to 1 if the error ID field is valid.                |
|                     |Set to 0 if the error ID field is not valid.            |
|                     |Bits[22:8] Error ID(Agent ID of the System).            |
|                     |Bit[7:0] Reason                                         |
|                     |See the SRESR register description in the System        |
|                     |Reset Controller (SRC) section in SoC reference mannual |
|                     |One reason maps to BIT(reason) in SRESR                 |
+---------------------+--------------------------------------------------------+
|uint32 extinfo[3]    |Array of extended info words(e.g. fault pc)             |
+---------------------+--------------------------------------------------------+

LMM_POWER_ON
~~~~~~~~~~~~

message_id: 0xB
protocol_id: 0x80
This command is mandatory.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 lmid       |ID of the Logical Machine                                  |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if LM successfully powers on.                     |
|                  |NOT_FOUND: if lmid not points to a valid logical machine.  |
|                  |INVALID_PARAMETERS: if lmid is same as the caller.         |
|                  |DENIED: if the agent does not have permission to manage the|
|                  |the LM specified by lmid.                                  |
+------------------+-----------------------------------------------------------+

LMM_RESET_VECTOR_SET
~~~~~~~~~~~~~~~~~~~~

message_id: 0xC
protocol_id: 0x80
This command is mandatory.

+-----------------------+------------------------------------------------------+
|Parameters                                                                    |
+-----------------------+------------------------------------------------------+
|Name                   |Description                                           |
+-----------------------+------------------------------------------------------+
|uint32 lmid            |ID of the Logical Machine                             |
+-----------------------+------------------------------------------------------+
|uint32 cpuid           |ID of the CPU inside the LM                           |
+-----------------------+------------------------------------------------------+
|uint32 flags           |Reset vector flags                                    |
|                       |Bits[31:0] Reserved, must be zero.                    |
+-----------------------+------------------------------------------------------+
|uint32 resetVectorLow  |Lower vector                                          |
+-----------------------+------------------------------------------------------+
|uint32 resetVectorHigh |Higher vector                                         |
+-----------------------+------------------------------------------------------+
|Return values                                                                 |
+-----------------------+------------------------------------------------------+
|Name                   |Description                                           |
+-----------------------+------------------------------------------------------+
|int32 status           |SUCCESS: If reset vector is set successfully.         |
|                       |NOT_FOUND: if lmid not points to a valid logical      |
|                       |machine, or cpuId is not valid.                       |
|                       |INVALID_PARAMETERS: if reset vector is invalid.       |
|                       |DENIED: if the agent does not have permission to set  |
|                       |the reset vector for the CPU in the LM.               |
+-----------------------+------------------------------------------------------+

NEGOTIATE_PROTOCOL_VERSION
~~~~~~~~~~~~~~~~~~~~~~~~~~~

message_id: 0x10
protocol_id: 0x80
This command is mandatory.

+--------------------+---------------------------------------------------------+
|Parameters                                                                    |
+--------------------+---------------------------------------------------------+
|Name                |Description                                              |
+--------------------+---------------------------------------------------------+
|uint32 version      |The negotiated protocol version the agent intends to use |
+--------------------+---------------------------------------------------------+
|Return values                                                                 |
+--------------------+---------------------------------------------------------+
|Name                |Description                                              |
+--------------------+---------------------------------------------------------+
|int32 status        |SUCCESS: if the negotiated protocol version is supported |
|                    |by the platform. All commands, responses, and            |
|                    |notifications post successful return of this command must|
|                    |comply with the negotiated version.                      |
|                    |NOT_SUPPORTED: if the protocol version is not supported. |
+--------------------+---------------------------------------------------------+

Notifications
_____________

LMM_EVENT
~~~~~~~~~

message_id: 0x0
protocol_id: 0x80

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 lmid       |Identifier for the LM that caused the transition.          |
+------------------+-----------------------------------------------------------+
|uint32 eventlm    |Identifier of the LM this event refers to.                 |
+------------------+-----------------------------------------------------------+
|uint32 flags      |LM events:                                                 |
|                  |Bits[31:3] Reserved, must be zero.                         |
|                  |Bit[3] Wake (resume) event:                                |
|                  |1 LM has awakened.                                         |
|                  |0 not a wake event.                                        |
|                  |Bit[2] Suspend (sleep) event:                              |
|                  |1 LM has suspended.                                        |
|                  |0 not a suspend event.                                     |
|                  |Bit[1] Shutdown (off) event:                               |
|                  |1 LM has shutdown.                                         |
|                  |0 not a shutdown event.                                    |
|                  |Bit[0] Boot (on) event:                                    |
|                  |1 LM has booted.                                           |
|                  |0 not a boot event.                                        |
+------------------+-----------------------------------------------------------+

SCMI_BBM: System Control and Management BBM Vendor Protocol
==============================================================

This protocol is intended provide access to the battery-backed module. This
contains persistent storage (GPR), an RTC, and the ON/OFF button. The protocol
can also provide access to similar functions implemented via external board
components. The BBM protocol provides functions to:

- Describe the protocol version.
- Discover implementation attributes.
- Read/write GPR
- Discover the RTCs available in the system.
- Read/write the RTC time in seconds and ticks
- Set an alarm (per LM) in seconds
- Get notifications on RTC update, alarm, or rollover.
- Get notification on ON/OFF button activity.

For most SoC, there is one on-chip RTC (e.g. in BBNSM) and this is RTC ID 0.
Board code can add additional GPR and RTC.

GPR are not aggregated. The RTC time is also not aggregated. Setting these
sets for all so normally exclusive access would be granted to one agent for
each. However, RTC alarms are maintained for each LM and the hardware is
programmed with the next nearest alarm time. So only one agent in an LM should
be given access rights to set an RTC alarm.

Commands:
_________

PROTOCOL_VERSION
~~~~~~~~~~~~~~~~

message_id: 0x0
protocol_id: 0x81

+---------------+--------------------------------------------------------------+
|Return values                                                                 |
+---------------+--------------------------------------------------------------+
|Name           |Description                                                   |
+---------------+--------------------------------------------------------------+
|int32 status   | See ARM SCMI Specification for status code definitions.      |
+---------------+--------------------------------------------------------------+
|uint32 version | For this revision of the specification, this value must be   |
|               | 0x10000.                                                     |
+---------------+--------------------------------------------------------------+

PROTOCOL_ATTRIBUTES
~~~~~~~~~~~~~~~~~~~

message_id: 0x1
protocol_id: 0x81

+---------------+--------------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      | See ARM SCMI Specification for status code definitions.   |
+------------------+-----------------------------------------------------------+
|uint32 attributes | Bits[31:8] Number of RTCs.                                |
|                  | Bits[15:0] Number of persistent storage (GPR) words.      |
+------------------+-----------------------------------------------------------+

PROTOCOL_MESSAGE_ATTRIBUTES
~~~~~~~~~~~~~~~~~~~~~~~~~~~

message_id: 0x2
protocol_id: 0x81

+---------------+--------------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: in case the message is implemented and available  |
|                  |to use.                                                    |
|                  |NOT_FOUND: if the message identified by message_id is      |
|                  |invalid or not implemented                                 |
+------------------+-----------------------------------------------------------+
|uint32 attributes |Flags that are associated with a specific function in the  |
|                  |protocol. For all functions in this protocol, this         |
|                  |parameter has a value of 0                                 |
+------------------+-----------------------------------------------------------+

BBM_GPR_SET
~~~~~~~~~~~

message_id: 0x3
protocol_id: 0x81

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 index      |Index of GPR to write                                      |
+------------------+-----------------------------------------------------------+
|uint32 value      |32-bit value to write to the GPR                           |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if the GPR was successfully written.              |
|                  |NOT_FOUND: if the index is not valid.                      |
|                  |DENIED: if the agent does not have permission to write     |
|                  |the specified GPR                                          |
+------------------+-----------------------------------------------------------+

BBM_GPR_GET
~~~~~~~~~~~

message_id: 0x4
protocol_id: 0x81

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 index      |Index of GPR to read                                       |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if the GPR was successfully read.                 |
|                  |NOT_FOUND: if the index is not valid.                      |
|                  |DENIED: if the agent does not have permission to read      |
|                  |the specified GPR.                                         |
+------------------+-----------------------------------------------------------+
|uint32 value      |32-bit value read from the GPR                             |
+------------------+-----------------------------------------------------------+

BBM_RTC_ATTRIBUTES
~~~~~~~~~~~~~~~~~~

message_id: 0x5
protocol_id: 0x81

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 index      |Index of RTC                                               |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: returned the attributes.                          |
|                  |NOT_FOUND: Index is invalid.                               |
+------------------+-----------------------------------------------------------+
|uint32 attributes |Bit[31:24] Bit width of RTC seconds.                       |
|                  |Bit[23:16] Bit width of RTC ticks.                         |
|                  |Bits[15:0] RTC ticks per second                            |
+------------------+-----------------------------------------------------------+
|uint8 name[16]    |Null-terminated ASCII string of up to 16 bytes in length   |
|                  |describing the RTC name                                    |
+------------------+-----------------------------------------------------------+

BBM_RTC_TIME_SET
~~~~~~~~~~~~~~~~

message_id: 0x6
protocol_id: 0x81

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 index      |Index of RTC                                               |
+------------------+-----------------------------------------------------------+
|uint32 flags      |Bits[31:1] Reserved, must be zero.                         |
|                  |Bit[0] RTC time format:                                    |
|                  |Set to 1 if the time is in ticks.                          |
|                  |Set to 0 if the time is in seconds                         |
+------------------+-----------------------------------------------------------+
|uint32 time[2]    |Lower word: Lower 32 bits of the time in seconds/ticks.    |
|                  |Upper word: Upper 32 bits of the time in seconds/ticks.    |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: RTC time was successfully set.                    |
|                  |NOT_FOUND: rtcId pertains to a non-existent RTC.           |
|                  |INVALID_PARAMETERS: time is not valid                      |
|                  |(beyond the range of the RTC).                             |
|                  |DENIED: the agent does not have permission to set the RTC. |
+------------------+-----------------------------------------------------------+

BBM_RTC_TIME_GET
~~~~~~~~~~~~~~~~

message_id: 0x7
protocol_id: 0x81

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 index      |Index of RTC                                               |
+------------------+-----------------------------------------------------------+
|uint32 flags      |Bits[31:1] Reserved, must be zero.                         |
|                  |Bit[0] RTC time format:                                    |
|                  |Set to 1 if the time is in ticks.                          |
|                  |Set to 0 if the time is in seconds                         |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: RTC time was successfully get.                    |
|                  |NOT_FOUND: rtcId pertains to a non-existent RTC.           |
+------------------+-----------------------------------------------------------+
|uint32 time[2]    |Lower word: Lower 32 bits of the time in seconds/ticks.    |
|                  |Upper word: Upper 32 bits of the time in seconds/ticks.    |
+------------------+-----------------------------------------------------------+

BBM_RTC_ALARM_SET
~~~~~~~~~~~~~~~~~

message_id: 0x8
protocol_id: 0x81

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 index      |Index of RTC                                               |
+------------------+-----------------------------------------------------------+
|uint32 flags      |Bits[31:1] Reserved, must be zero.                         |
|                  |Bit[0] RTC enable flag:                                    |
|                  |Set to 1 if the RTC alarm should be enabled.               |
|                  |Set to 0 if the RTC alarm should be disabled               |
+------------------+-----------------------------------------------------------+
|uint32 time[2]    |Lower word: Lower 32 bits of the time in seconds.          |
|                  |Upper word: Upper 32 bits of the time in seconds.          |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: RTC time was successfully set.                    |
|                  |NOT_FOUND: rtcId pertains to a non-existent RTC.           |
|                  |INVALID_PARAMETERS: time is not valid                      |
|                  |(beyond the range of the RTC).                             |
|                  |DENIED: the agent does not have permission to set the RTC  |
|                  |alarm                                                      |
+------------------+-----------------------------------------------------------+

BBM_BUTTON_GET
~~~~~~~~~~~~~~

message_id: 0x9
protocol_id: 0x81

+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if the button status was read.                    |
|                  |Other value: ARM SCMI Specification status code definitions|
+------------------+-----------------------------------------------------------+
|uint32 state      |State of the ON/OFF button. 1: ON, 0: OFF                  |
+------------------+-----------------------------------------------------------+

BBM_RTC_NOTIFY
~~~~~~~~~~~~~~

message_id: 0xA
protocol_id: 0x81

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 index      |Index of RTC                                               |
+------------------+-----------------------------------------------------------+
|uint32 flags      |Notification flags                                         |
|                  |Bits[31:3] Reserved, must be zero.                         |
|                  |Bit[2] Update enable:                                      |
|                  |Set to 1 to send notification.                             |
|                  |Set to 0 if no notification.                               |
|                  |Bit[1] Rollover enable:                                    |
|                  |Set to 1 to send notification.                             |
|                  |Set to 0 if no notification.                               |
|                  |Bit[0] Alarm enable:                                       |
|                  |Set to 1 to send notification.                             |
|                  |Set to 0 if no notification                                |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: notification configuration was successfully       |
|                  |updated.                                                   |
|                  |NOT_FOUND: rtcId pertains to a non-existent RTC.           |
|                  |DENIED: the agent does not have permission to request RTC  |
|                  |notifications.                                             |
+------------------+-----------------------------------------------------------+

BBM_BUTTON_NOTIFY
~~~~~~~~~~~~~~~~~

message_id: 0xB
protocol_id: 0x81

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 flags      |Notification flags                                         |
|                  |Bits[31:1] Reserved, must be zero.                         |
|                  |Bit[0] Enable button:                                      |
|                  |Set to 1 to send notification.                             |
|                  |Set to 0 if no notification                                |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: notification configuration was successfully       |
|                  |updated.                                                   |
|                  |DENIED: the agent does not have permission to request      |
|                  |button notifications.                                      |
+------------------+-----------------------------------------------------------+

NEGOTIATE_PROTOCOL_VERSION
~~~~~~~~~~~~~~~~~~~~~~~~~~

message_id: 0x10
protocol_id: 0x81

+--------------------+---------------------------------------------------------+
|Parameters                                                                    |
+--------------------+---------------------------------------------------------+
|Name                |Description                                              |
+--------------------+---------------------------------------------------------+
|uint32 version      |The negotiated protocol version the agent intends to use |
+--------------------+---------------------------------------------------------+
|Return values                                                                 |
+--------------------+---------------------------------------------------------+
|Name                |Description                                              |
+--------------------+---------------------------------------------------------+
|int32 status        |SUCCESS: if the negotiated protocol version is supported |
|                    |by the platform. All commands, responses, and            |
|                    |notifications post successful return of this command must|
|                    |comply with the negotiated version.                      |
|                    |NOT_SUPPORTED: if the protocol version is not supported. |
+--------------------+---------------------------------------------------------+

Notifications
_____________

BBM_RTC_EVENT
~~~~~~~~~~~~~

message_id: 0x0
protocol_id: 0x81

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 flags      |RTC events:                                                |
|                  |Bits[31:2] Reserved, must be zero.                         |
|                  |Bit[1] RTC rollover notification:                          |
|                  |1 RTC rollover detected.                                   |
|                  |0 no RTC rollover detected.                                |
|                  |Bit[0] RTC alarm notification:                             |
|                  |1 RTC alarm generated.                                     |
|                  |0 no RTC alarm generated.                                  |
+------------------+-----------------------------------------------------------+

BBM_BUTTON_EVENT
~~~~~~~~~~~~~~~~

message_id: 0x1
protocol_id: 0x81

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 flags      |RTC events:                                                |
+------------------+-----------------------------------------------------------+
|                  |Button events:                                             |
|                  |Bits[31:1] Reserved, must be zero.                         |
|                  |Bit[0] Button notification:                                |
|                  |1 button change detected.                                  |
|                  |0 no button change detected.                               |
+------------------+-----------------------------------------------------------+

System Control and Management CPU Vendor Protocol
=================================================

This protocol allows an agent to start or stop a CPU. It is used to manage
auxiliary CPUs in a target LM (e.g. additional cores in an AP cluster or
Cortex-M cores).
Note:
 - For cores in AP cluster, PSCI should be used and PSCI firmware will use CPU
   protocol to handle them. For cores in non-AP cluster, Operating System(e.g.
   Linux OS) could use CPU protocols to control Cortex-M7 cores.
 - CPU indicates the core and its auxiliary peripherals(e.g. TCM) inside
   i.MX SoC

There are cases where giving an agent full control of a CPU via the CPU
protocol is not desired. The LMM protocol is more restricted to just boot,
shutdown, etc. So an agent might boot another logical machine but not be
able to directly mess the state of its CPUs. Its also the reason there is an
LMM power on command even though that could have been done through the
power protocol.

The CPU protocol provides commands to:

- Describe the protocol version.
- Discover implementation attributes.
- Discover the CPUs defined in the system.
- Start a CPU.
- Stop a CPU.
- Set the boot and resume addresses for a CPU.
- Set the sleep mode of a CPU.
- Configure wake-up sources for a CPU.
- Configure power domain reactions (LPM mode and retention mask) for a CPU.
- The CPU IDs can be found in the CPU section of the SoC DEVICE: SM Device
  Interface. They can also be found in the SoC RM. See the CPU Mode Control
  (CMC) list in General Power Controller (GPC) section.

CPU settings are not aggregated and setting their state is normally exclusive
to one client.

Commands:
_________

PROTOCOL_VERSION
~~~~~~~~~~~~~~~~

message_id: 0x0
protocol_id: 0x82
This command is mandatory.

+---------------+--------------------------------------------------------------+
|Return values                                                                 |
+---------------+--------------------------------------------------------------+
|Name           |Description                                                   |
+---------------+--------------------------------------------------------------+
|int32 status   | See ARM SCMI Specification for status code definitions.      |
+---------------+--------------------------------------------------------------+
|uint32 version | For this revision of the specification, this value must be   |
|               | 0x10000.                                                     |
+---------------+--------------------------------------------------------------+

PROTOCOL_ATTRIBUTES
~~~~~~~~~~~~~~~~~~~

message_id: 0x1
protocol_id: 0x82
This command is mandatory.

+---------------+--------------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      | See ARM SCMI Specification for status code definitions.   |
+------------------+-----------------------------------------------------------+
|uint32 attributes |Protocol attributes:                                       |
|                  |Bits[31:16] Reserved, must be zero.                        |
|                  |Bits[15:0] Number of CPUs                                  |
+------------------+-----------------------------------------------------------+

PROTOCOL_MESSAGE_ATTRIBUTES
~~~~~~~~~~~~~~~~~~~~~~~~~~~

message_id: 0x2
protocol_id: 0x82
This command is mandatory.

+---------------+--------------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: in case the message is implemented and available  |
|                  |to use.                                                    |
|                  |NOT_FOUND: if the message identified by message_id is      |
|                  |invalid or not implemented                                 |
+------------------+-----------------------------------------------------------+
|uint32 attributes |Flags that are associated with a specific command in the   |
|                  |protocol. For all commands in this protocol, this          |
|                  |parameter has a value of 0                                 |
+------------------+-----------------------------------------------------------+

CPU_ATTRIBUTES
~~~~~~~~~~~~~~

message_id: 0x4
protocol_id: 0x82
This command is mandatory.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 cpuid      |Identifier for the CPU                                     |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if valid attributes are returned successfully.    |
|                  |NOT_FOUND: if the cpuid is not valid.                      |
+------------------+-----------------------------------------------------------+
|uint32 attributes |Bits[31:0] Reserved, must be zero                          |
+------------------+-----------------------------------------------------------+
|char name[16]     |NULL terminated ASCII string with CPU name up to 16 bytes  |
+------------------+-----------------------------------------------------------+

CPU_START
~~~~~~~~~

message_id: 0x4
protocol_id: 0x82
This command is mandatory.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 cpuid      |Identifier for the CPU                                     |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if the cpu is started successfully.               |
|                  |NOT_FOUND: if cpuid is not valid.                          |
|                  |DENIED: the calling agent is not allowed to start this CPU.|
+------------------+-----------------------------------------------------------+

CPU_STOP
~~~~~~~~

message_id: 0x5
protocol_id: 0x82
This command is mandatory.

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 cpuid      |Identifier for the CPU                                     |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if the cpu is started successfully.               |
|                  |NOT_FOUND: if cpuid is not valid.                          |
|                  |DENIED: the calling agent is not allowed to stop this CPU. |
+------------------+-----------------------------------------------------------+

CPU_RESET_VECTOR_SET
~~~~~~~~~~~~~~~~~~~~

message_id: 0x6
protocol_id: 0x82
This command is mandatory.

+----------------------+-------------------------------------------------------+
|Parameters                                                                    |
+----------------------+-------------------------------------------------------+
|Name                  |Description                                            |
+----------------------+-------------------------------------------------------+
|uint32 cpuid          |Identifier for the CPU                                 |
+----------------------+-------------------------------------------------------+
|uint32 flags          |Reset vector flags:                                    |
|                      |Bit[31] Resume flag.                                   |
|                      |Set to 1 to update the reset vector used on resume.    |
|                      |Bit[30] Boot flag.                                     |
|                      |Set to 1 to update the reset vector used for boot.     |
|                      |Bits[29:1] Reserved, must be zero.                     |
|                      |Bit[0] Table flag.                                     |
|                      |Set to 1 if vector is the vector table base address.   |
+----------------------+-------------------------------------------------------+
|uint32 resetVectorLow |Lower vector:                                          |
|                      |If bit[0] of flags is 0, the lower 32 bits of the      |
|                      |physical address where the CPU should execute from on  |
|                      |reset. If bit[0] of flags is 1, the lower 32 bits of   |
|                      |the vector table base address                          |
+----------------------+-------------------------------------------------------+
|uint32 resetVectorhigh|Upper vector:                                          |
|                      |If bit[0] of flags is 0, the upper 32 bits of the      |
|                      |physical address where the CPU should execute from on  |
|                      |reset. If bit[0] of flags is 1, the upper 32 bits of   |
|                      |the vector table base address                          |
+----------------------+-------------------------------------------------------+
|Return values                                                                 |
+----------------------+-------------------------------------------------------+
|Name                  |Description                                            |
+----------------------+-------------------------------------------------------+
|int32 status          |SUCCESS: if the CPU reset vector is set successfully.  |
|                      |NOT_FOUND: if cpuId does not point to a valid CPU.     |
|                      |INVALID_PARAMETERS: the requested vector type is not   |
|                      |supported by this CPU.                                 |
|                      |DENIED: the calling agent is not allowed to set the    |
|                      |reset vector of this CPU                               |
+----------------------+-------------------------------------------------------+

CPU_SLEEP_MODE_SET
~~~~~~~~~~~~~~~~~~

message_id: 0x7
protocol_id: 0x82
This command is mandatory.

+----------------------+-------------------------------------------------------+
|Parameters                                                                    |
+----------------------+-------------------------------------------------------+
|Name                  |Description                                            |
+----------------------+-------------------------------------------------------+
|uint32 cpuid          |Identifier for the CPU                                 |
+----------------------+-------------------------------------------------------+
|uint32 flags          |Sleep mode flags:                                      |
|                      |Bits[31:1] Reserved, must be zero.                     |
|                      |Bit[0] IRQ mux:                                        |
|                      |If set to 1 the wakeup mux source is the GIC, else if 0|
|                      |then the GPC                                           |
+----------------------+-------------------------------------------------------+
|uint32 sleepmode      |target sleep mode. When CPU runs into WFI, the GPC mode|
|                      |will be triggered to be in below modes:                |
|                      |RUN:     (0)                                           |
|                      |WAIT:    (1)                                           |
|                      |STOP:    (2)                                           |
|                      |SUSPEND: (3)                                           |
+----------------------+-------------------------------------------------------+
|Return values                                                                 |
+----------------------+-------------------------------------------------------+
|Name                  |Description                                            |
+----------------------+-------------------------------------------------------+
|int32 status          |SUCCESS: if the CPU sleep mode is set successfully.    |
|                      |NOT_FOUND: if cpuId does not point to a valid CPU.     |
|                      |INVALID_PARAMETERS: the sleepmode or flags is invalid. |
|                      |DENIED: the calling agent is not allowed to configure  |
|                      |the CPU                                                |
+----------------------+-------------------------------------------------------+

CPU_INFO_GET
~~~~~~~~~~~~

message_id: 0xC
protocol_id: 0x82
This command is mandatory.

+----------------------+-------------------------------------------------------+
|Parameters                                                                    |
+----------------------+-------------------------------------------------------+
|Name                  |Description                                            |
+----------------------+-------------------------------------------------------+
|uint32 cpuid          |Identifier for the CPU                                 |
+----------------------+-------------------------------------------------------+
|Return values                                                                 |
+----------------------+-------------------------------------------------------+
|Name                  |Description                                            |
+----------------------+-------------------------------------------------------+
|int32 status          |SUCCESS: if valid attributes are returned successfully.|
|                      |NOT_FOUND: if the cpuid is not valid.                  |
+----------------------+-------------------------------------------------------+
|uint32 runmode        |Run mode for the CPU                                   |
|                      |RUN(0):cpu started                                     |
|                      |HOLD(1):cpu powered up and reset asserted              |
|                      |STOP(2):cpu reseted and hold cpu                       |
|                      |SUSPEND(3):in cpuidle state                            |
+----------------------+-------------------------------------------------------+
|uint32 sleepmode      |Sleep mode for the CPU, see CPU_SLEEP_MODE_SET         |
+----------------------+-------------------------------------------------------+
|uint32 resetvectorlow |Reset vector low 32 bits for the CPU                   |
+----------------------+-------------------------------------------------------+
|uint32 resetvecothigh |Reset vector high 32 bits for the CPU                  |
+----------------------+-------------------------------------------------------+

NEGOTIATE_PROTOCOL_VERSION
~~~~~~~~~~~~~~~~~~~~~~~~~~

message_id: 0x10
protocol_id: 0x82
This command is mandatory.

+--------------------+---------------------------------------------------------+
|Parameters                                                                    |
+--------------------+---------------------------------------------------------+
|Name                |Description                                              |
+--------------------+---------------------------------------------------------+
|uint32 version      |The negotiated protocol version the agent intends to use |
+--------------------+---------------------------------------------------------+
|Return values                                                                 |
+--------------------+---------------------------------------------------------+
|Name                |Description                                              |
+--------------------+---------------------------------------------------------+
|int32 status        |SUCCESS: if the negotiated protocol version is supported |
|                    |by the platform. All commands, responses, and            |
|                    |notifications post successful return of this command must|
|                    |comply with the negotiated version.                      |
|                    |NOT_SUPPORTED: if the protocol version is not supported. |
+--------------------+---------------------------------------------------------+

SCMI_MISC: System Control and Management MISC Vendor Protocol
================================================================

Provides miscellaneous functions. This includes controls that are miscellaneous
settings/actions that must be exposed from the SM to agents. They are device
specific and are usually define to access bit fields in various mix block
control modules, IOMUX_GPR, and other GPR/CSR owned by the SM. This protocol
supports the following functions:

- Describe the protocol version.
- Discover implementation attributes.
- Set/Get a control.
- Initiate an action on a control.
- Obtain platform (i.e. SM) build information.
- Obtain ROM passover data.
- Read boot/shutdown/reset information for the LM or the system.

Commands:
_________

PROTOCOL_VERSION
~~~~~~~~~~~~~~~~

message_id: 0x0
protocol_id: 0x84

+---------------+--------------------------------------------------------------+
|Return values                                                                 |
+---------------+--------------------------------------------------------------+
|Name           |Description                                                   |
+---------------+--------------------------------------------------------------+
|int32 status   | See ARM SCMI Specification for status code definitions.      |
+---------------+--------------------------------------------------------------+
|uint32 version | For this revision of the specification, this value must be   |
|               | 0x10000.                                                     |
+---------------+--------------------------------------------------------------+

PROTOCOL_ATTRIBUTES
~~~~~~~~~~~~~~~~~~~

message_id: 0x1
protocol_id: 0x84

+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      | See ARM SCMI Specification for status code definitions.   |
+------------------+-----------------------------------------------------------+
|uint32 attributes |Protocol attributes:                                       |
|                  |Bits[31:24] Reserved, must be zero.                        |
|                  |Bits[23:16] Number of reset reasons.                       |
|                  |Bits[15:0] Number of controls                              |
+------------------+-----------------------------------------------------------+

PROTOCOL_MESSAGE_ATTRIBUTES
~~~~~~~~~~~~~~~~~~~~~~~~~~~

message_id: 0x2
protocol_id: 0x84

+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: in case the message is implemented and available  |
|                  |to use.                                                    |
|                  |NOT_FOUND: if the message identified by message_id is      |
|                  |invalid or not implemented                                 |
+------------------+-----------------------------------------------------------+
|uint32 attributes |Flags that are associated with a specific function in the  |
|                  |protocol. For all functions in this protocol, this         |
|                  |parameter has a value of 0                                 |
+------------------+-----------------------------------------------------------+

MISC_CONTROL_SET
~~~~~~~~~~~~~~~~

message_id: 0x3
protocol_id: 0x84

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 index      |Index of the control                                       |
+------------------+-----------------------------------------------------------+
|uint32 num        |Size of the value data in words                            |
+------------------+-----------------------------------------------------------+
|uint32 val[8]     |value data array                                           |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if the control was set successfully.              |
|                  |NOT_FOUND: if the index is not valid.                      |
|                  |DENIED: if the agent does not have permission to set the   |
|                  |control                                                    |
+------------------+-----------------------------------------------------------+

MISC_CONTROL_GET
~~~~~~~~~~~~~~~~

message_id: 0x4
protocol_id: 0x84

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 index      |Index of the control                                       |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if the control was get successfully.              |
|                  |NOT_FOUND: if the index is not valid.                      |
|                  |DENIED: if the agent does not have permission to get the   |
|                  |control                                                    |
+------------------+-----------------------------------------------------------+
|uint32 num        |Size of the return data in words, max 8                    |
+------------------+-----------------------------------------------------------+
|uint32            |                                                           |
|val[0, num - 1]   |value data array                                           |
+------------------+-----------------------------------------------------------+

MISC_CONTROL_ACTION
~~~~~~~~~~~~~~~~~~~

message_id: 0x5
protocol_id: 0x84

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 index      |Index of the control                                       |
+------------------+-----------------------------------------------------------+
|uint32 action	   |Action for the control                                     |
+------------------+-----------------------------------------------------------+
|uint32 numarg	   |Size of the argument data, max 8                           |
+------------------+-----------------------------------------------------------+
|uint32            |                                                           |
|arg[0, numarg -1] |Argument data array                                        |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if the action was set successfully.               |
|                  |NOT_FOUND: if the index is not valid.                      |
|                  |DENIED: if the agent does not have permission to get the   |
|                  |control                                                    |
+------------------+-----------------------------------------------------------+
|uint32 num        |Size of the return data in words, max 8                    |
+------------------+-----------------------------------------------------------+
|uint32            |                                                           |
|val[0, num - 1]   |value data array                                           |
+------------------+-----------------------------------------------------------+

MISC_DISCOVER_BUILD_INFO
~~~~~~~~~~~~~~~~~~~~~~~~

This function is used to obtain the build commit, data, time, number.

message_id: 0x6
protocol_id: 0x84

+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if the build info was got successfully.           |
|                  |NOT_SUPPORTED: if the data is not available.               |
+------------------+-----------------------------------------------------------+
|uint32 buildnum   |Build number                                               |
+------------------+-----------------------------------------------------------+
|uint32 buildcommit|Most significant 32 bits of the git commit hash            |
+------------------+-----------------------------------------------------------+
|uint8 date[16]    |Date of build. Null terminated ASCII string of up to 16    |
|                  |bytes in length                                            |
+------------------+-----------------------------------------------------------+
|uint8 time[16]    |Time of build. Null terminated ASCII string of up to 16    |
|                  |bytes in length                                            |
+------------------+-----------------------------------------------------------+

MISC_ROM_PASSOVER_GET
~~~~~~~~~~~~~~~~~~~~~

ROM passover data is information exported by ROM and could be used by others.
It includes boot device, instance, type, mode and etc. This function is used
to obtain the ROM passover data. The returned block of words is structured as
defined in the ROM passover section in the SoC RM.

message_id: 0x7
protocol_id: 0x84

+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if the data was got successfully.                 |
|                  |NOT_SUPPORTED: if the data is not available.               |
+------------------+-----------------------------------------------------------+
|uint32 num        |Size of the passover data in words, max 13                 |
+------------------+-----------------------------------------------------------+
|uint32            |                                                           |
|data[0, num - 1]  |Passover data array                                        |
+------------------+-----------------------------------------------------------+

MISC_CONTROL_NOTIFY
~~~~~~~~~~~~~~~~~~~

message_id: 0x8
protocol_id: 0x84

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 index      |Index of control                                           |
+------------------+-----------------------------------------------------------+
|uint32 flags      |Notification flags, varies by control                      |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: notification configuration was successfully       |
|                  |updated.                                                   |
|                  |NOT_FOUND: control id not exists.                          |
|                  |INVALID_PARAMETERS: if the input attributes flag specifies |
|                  |unsupported or invalid configurations..                    |
|                  |DENIED: if the calling agent is not permitted to request   |
|                  |the notification.                                          |
+------------------+-----------------------------------------------------------+

MISC_RESET_REASON_ATTRIBUTES
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

message_id: 0x9
protocol_id: 0x84

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 reasonid   |Identifier for the reason                                  |
+------------------+-----------------------------------------------------------+
|Return values                                                                 |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|int32 status      |SUCCESS: if valid reason attributes are returned           |
|                  |NOT_FOUND: if reasonId pertains to a non-existent reason.  |
+------------------+-----------------------------------------------------------+
|uint32 attributes |Reason attributes. This parameter has the following        |
|                  |format: Bits[31:0] Reserved, must be zero                  |
|                  |Bits[15:0] Number of persistent storage (GPR) words.       |
+------------------+-----------------------------------------------------------+
|uint8 name[16]    |Null-terminated ASCII string of up to 16 bytes in length   |
|                  |describing the reason                                      |
+------------------+-----------------------------------------------------------+

MISC_RESET_REASON_GET
~~~~~~~~~~~~~~~~~~~~~

message_id: 0xA
protocol_id: 0x84

+--------------------+---------------------------------------------------------+
|Parameters                                                                    |
+--------------------+---------------------------------------------------------+
|Name                |Description                                              |
+--------------------+---------------------------------------------------------+
|uint32 flags        |Reason flags. This parameter has the following format:   |
|                    |Bits[31:1] Reserved, must be zero.                       |
|                    |Bit[0] System:                                           |
|                    |Set to 1 to return the system reason.                    |
|                    |Set to 0 to return the LM reason                         |
+--------------------+---------------------------------------------------------+
|Return values                                                                 |
+--------------------+---------------------------------------------------------+
|Name                |Description                                              |
+--------------------+---------------------------------------------------------+
|int32 status        |SUCCESS: reset reason return                             |
+--------------------+---------------------------------------------------------+
|uint32 bootflags    |Boot reason flags. This parameter has the format:        |
|                    |Bits[31] Valid.                                          |
|                    |Set to 1 if the entire reason is valid.                  |
|                    |Set to 0 if the entire reason is not valid.              |
|                    |Bits[30:29] Reserved, must be zero.                      |
|                    |Bit[28] Valid origin:                                    |
|                    |Set to 1 if the origin field is valid.                   |
|                    |Set to 0 if the origin field is not valid.               |
|                    |Bits[27:24] Origin.                                      |
|                    |Bit[23] Valid err ID:                                    |
|                    |Set to 1 if the error ID field is valid.                 |
|                    |Set to 0 if the error ID field is not valid.             |
|                    |Bits[22:8] Error ID.                                     |
|                    |Bit[7:0] Reason                                          |
+--------------------+---------------------------------------------------------+
|uint32 shutdownflags|Shutdown reason flags. This parameter has the format:    |
|                    |Bits[31] Valid.                                          |
|                    |Set to 1 if the entire reason is valid.                  |
|                    |Set to 0 if the entire reason is not valid.              |
|                    |Bits[30:29] Number of valid extended info words.         |
|                    |Bit[28] Valid origin:                                    |
|                    |Set to 1 if the origin field is valid.                   |
|                    |Set to 0 if the origin field is not valid.               |
|                    |Bits[27:24] Origin.                                      |
|                    |Bit[23] Valid err ID:                                    |
|                    |Set to 1 if the error ID field is valid.                 |
|                    |Set to 0 if the error ID field is not valid.             |
|                    |Bits[22:8] Error ID.                                     |
|                    |Bit[7:0] Reason                                          |
+--------------------+---------------------------------------------------------+
|uint32 extinfo[8]   |Array of extended info words                             |
+--------------------+---------------------------------------------------------+

MISC_SI_INFO_GET
~~~~~~~~~~~~~~~~

message_id: 0xB
protocol_id: 0x84

+--------------------+---------------------------------------------------------+
|Return values                                                                 |
+--------------------+---------------------------------------------------------+
|Name                |Description                                              |
+--------------------+---------------------------------------------------------+
|int32 status        |SUCCESS: silicon info return                             |
+--------------------+---------------------------------------------------------+
|uint32 deviceid     |Silicon specific device ID                               |
+--------------------+---------------------------------------------------------+
|uint32 sirev        |Silicon specific revision                                |
+--------------------+---------------------------------------------------------+
|uint32 partnum      |Silicon specific part number                             |
+--------------------+---------------------------------------------------------+
|uint8 siname[16]    |Silicon name/revision. Null terminated ASCII string of up|
|                    |to 16 bytes in length                                    |
+--------------------+---------------------------------------------------------+

MISC_CFG_INFO_GET
~~~~~~~~~~~~~~~~~

message_id: 0xC
protocol_id: 0x84

+--------------------+---------------------------------------------------------+
|Return values                                                                 |
+--------------------+---------------------------------------------------------+
|Name                |Description                                              |
+--------------------+---------------------------------------------------------+
|int32 status        |SUCCESS: config name return                              |
|                    |NOT_SUPPORTED: name not available                        |
+--------------------+---------------------------------------------------------+
|uint32 msel         |Mode selector value                                      |
+--------------------+---------------------------------------------------------+
|uint8 cfgname[16]   |config file basename. Null terminated ASCII string of up |
|                    |to 16 bytes in length                                    |
+--------------------+---------------------------------------------------------+

MISC_SYSLOG_GET
~~~~~~~~~~~~~~~

message_id: 0xD
protocol_id: 0x84

+--------------------+---------------------------------------------------------+
|Parameters                                                                    |
+--------------------+---------------------------------------------------------+
|Name                |Description                                              |
+--------------------+---------------------------------------------------------+
|uint32 flags        |Device specific flags that might impact the data returned|
|                    |or clearing of the data                                  |
+--------------------+---------------------------------------------------------+
|uint32 logindex     |Index to the first log word. Will be the first element in|
|                    |the return array                                         |
+--------------------+---------------------------------------------------------+
|Return values                                                                 |
+--------------------+---------------------------------------------------------+
|Name                |Description                                              |
+--------------------+---------------------------------------------------------+
|int32 status        |SUCCESS: system log return                               |
+--------------------+---------------------------------------------------------+
|uint32 numLogflags  |Descriptor for the log data returned by this call.       |
|                    |Bits[31:20] Number of remaining log words.               |
|                    |Bits[15:12] Reserved, must be zero.                      |
|                    |Bits[11:0] Number of log words that are returned by this |
|                    |call                                                     |
+--------------------+---------------------------------------------------------+
|uint32 syslog[N]    |Log data array, N is defined in bits[11:0] of numLogflags|
+--------------------+---------------------------------------------------------+

NEGOTIATE_PROTOCOL_VERSION
~~~~~~~~~~~~~~~~~~~~~~~~~~

message_id: 0x10
protocol_id: 0x84

+--------------------+---------------------------------------------------------+
|Parameters                                                                    |
+--------------------+---------------------------------------------------------+
|Name                |Description                                              |
+--------------------+---------------------------------------------------------+
|uint32 version      |The negotiated protocol version the agent intends to use |
+--------------------+---------------------------------------------------------+
|Return values                                                                 |
+--------------------+---------------------------------------------------------+
|Name                |Description                                              |
+--------------------+---------------------------------------------------------+
|int32 status        |SUCCESS: if the negotiated protocol version is supported |
|                    |by the platform. All commands, responses, and            |
|                    |notifications post successful return of this command must|
|                    |comply with the negotiated version.                      |
|                    |NOT_SUPPORTED: if the protocol version is not supported. |
+--------------------+---------------------------------------------------------+

Notifications
_____________

MISC_CONTROL_EVENT
~~~~~~~~~~~~~~~~~~

message_id: 0x0
protocol_id: 0x81

+------------------+-----------------------------------------------------------+
|Parameters                                                                    |
+------------------+-----------------------------------------------------------+
|Name              |Description                                                |
+------------------+-----------------------------------------------------------+
|uint32 ctrlid     |Identifier for the control that caused the event.          |
+------------------+-----------------------------------------------------------+
|uint32 flags      |Event flags, varies by control.                            |
+------------------+-----------------------------------------------------------+
