# PS5 ezRemote Server

This is a payload that runs in the background to allow installing PS4 packages from ftp/sftp, nfs, smb, webdav, http servers (rclone, MS IIS, apache, nginx, npxserve), github, archive.org and any direct http links.

This is mainly used with ezRemote Client which allows you to browse files on the remote servers and submit request to ezRemote Server for installing.

This payload is auto-loaded by ezRemote Client when it starts up or can be added to any other audo payload loaders like ps5_y2jb_autoloader or ps5_lua_loader etc...