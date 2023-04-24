
## Object Block

__Object_Block__ contains an object with some object metadata.

__Object_Block__ start with a _Object header_ followed by the object bytes: [header(8Bytes)][object(nBytes)...]

_Object header_ is defined by 8 bytes containing:
- hasAnaliticsTrace (1bit): define that object is followed by analytics structure
- hasSecurityPadding (1bit): define that object is followed by a padding of security (canary)
- retentionCounter (8bit): define how many times the object shall be dispose to be really deleted
- weakRetentionCounter (6bit): define how many times the object shall be unlocked before the memory is reusable
- lock (1bit): general multithread protection for this object

### AnaliticsTrace

Block: [header(8Bytes)][object(nBytes)...][Timestamp(8Bytes)][Stackstamp(8Bytes)]

### SecurityPadding

Security padding structure: [SecurityCookie(xBytes)][SecurityPaddingSize(1Bytes)]

Block: [header(8Bytes)][object(nBytes)...][SecurityPadding(xBytes)...]
Block: [header(8Bytes)][object(nBytes)...][SecurityPadding(xBytes)...][Timestamp(8Bytes)][Stackstamp(8Bytes)]

### Retention counter

Retention counters are 32bit counter, the counters bits are located in two place:
- local counter (k+1 bits): is the k least significant bits of the counter, and a _extension flag_ (1 bit) defining if the counter have a external 32bits part.
- extended counter (32 bits): is the 32 most significant bits of the counter

The extended counter are allocated in __Counter_Extension_Table__, this table is a key-value table with counter key based on object address, and value is the 32bit extension for the counter.

To increment/decrement the counter:
- we increment the local k bits
- if a carry is generated then we create or find the counter entry in the __Counter_Extension_Table__, and apply it to extension bits.
- _extension flag_ shall be set while this entry exist in the table.

## Objects Region

An __Objects_Region__ is structure is as uniform huge __Object_Block__ span, it is associated to a descriptor in the __Descriptors_Region__, this region is split in small span managed by an __Objects_Page__.

__Objects_Page__ requirements:
- are small span of 32 objects.
- manage the relation between the __Object__ and __Objects_Region__.

Each __Objects_Region__ have an _active zone_, where objects are allocated, this _active zone_ size is a 2^n value.

The _active zone_ is split in 4 part, each part have a correspond to a _page bucket_.
When _active zone_ grow the  _page buckets_ are merged, when it reduce  _page buckets_ are splitted (much slower than merge).

When a _context_ require a new page we pick it in the first not empty bucket from small to biggest address.

### Objects page

## Memory Space Context

# Memory object lifecycle

Key concepts:
- Objects thread cache (alias __OTCache__):
- Objects shared cache (alias __OSCache__):
- Objects page (OPage):
- Objects Region (ORegion):

Object flow:
- Thread local:
- OTCache object lifecycle:



Each thread have a dedicated __OTCache__, which contains some object of each object classes.


Object lifecycle
