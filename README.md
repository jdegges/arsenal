# Arsenal

A distributed file system client written in C using FUSE and libssh2. Arsenal
clients are backed by any number of storage nodes running some SFTP service
(tested with OpenSSH ~5.5). Arsenal communicates with the storage backend using
libssh2's SFTP library. Storage nodes can be configured into arbitrary
distributed and mirrored sets so long as they adhere to a tree hierarchy.

## Configuration

Storage nodes can be flexibly configured into any tree hierarchy that suits your needs. The configuration file format supports four main XML tags: `<arsenal>`, `<distribute>`, `<mirror>`, and `<volume>`

* `<arsenal>`     There must be exactly one arsenal tag at the top level of each configuration file. All other tags must lie within this one.
* `<distribute>`  Non-terminal node. All child nodes have the same directory structure but each node contains a unique set of files.
* `<mirror>`      Non-terminal node. All child nodes have the same directory structure and same set of files.
* `<volume>`      Terminal node. Maps to a directory on a remote SFTP server. Must contain tags that identify and allow access to the remote server.
  * `<name>`         String identifying this volume
  * `<root>`         Root directory on remote server
  * `<address>`      IP address of remote server
  * `<port>`         Port to connect to on remote server
  * `<public_key>`   Path to local public key file to use for authentication
  * `<private_key>`  Path to local private key file to use for authentication
  * `<username>`     Username to use for authentication
  * `<passphrase>`   Used for authentication (optional)

## Examples

Simple single server configuration and usage:

    $ cat single.xml
    <?xml version="1.0"?>
    <arsenal>
      <volume>
        <name>my storage share</name>
        <root>/storage/share</root>
        <address>127.0.0.1</address>
        <port>22</port>
        <public_key>/home/user/.ssh/id_rsa.pub</public_key>
        <private_key>/home/user/.ssh/id_rsa</private_key>
        <username>user</username>
      </volume>
    </arsenal>
    $ ls /storage/share
    one  two
    $ ls /mnt
    $ arsenal -o cfg=single.xml /mnt
    $ ls /mnt
    one  two
    $ sudo umount /mnt
    $ ls /mnt
    $

Simple mirror configuration and usage:

    $ cat mirror.xml
    <?xml version="1.0"?>
    <arsenal>
      <mirror>
        <volume>
          <name>share one</name>
          <root>/storage/share/1</root>
          <address>127.0.0.1</address>
          <port>22</port>
          <public_key>/home/user/.ssh/id_rsa.pub</public_key>
          <private_key>/home/user/.ssh/id_rsa</private_key>
          <username>user</username>
        </volume>
        <volume>
          <name>share two</name>
          <root>/storage/share/2</root>
          <address>127.0.0.1</address>
          <port>22</port>
          <public_key>/home/user/.ssh/id_rsa.pub</public_key>
          <private_key>/home/user/.ssh/id_rsa</private_key>
          <username>user</username>
        </volume>
      </mirror>
    </arsenal>
    $ mkdir /storage/share/1
    $ mkdir /storage/share/2
    $ echo serving from one > /storage/share/1/test
    # of course, in a "real" mirrored setup both files would have the same content
    $ echo serving from two > /storage/share/2/test
    $ ls /mnt
    $ arsenal -o cfg=mirror.xml /mnt
    $ ls /mnt
    test
    $ cat /mnt/test
    serving from one
    $ cat /mnt/test
    serving from two


## Read only

There is no chance that Arsenal will ever corrupt or otherwise actively damage
your files. Directories are mounted strictly read only and no modification
operations are supported. Check out src/arsenal.c, only read operations are
hooked into the FUSE layer.
