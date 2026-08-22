
![APP NAME](https://github.com/RiRiRonRon/RakshaHub/blob/main/ascii-art-text.png?raw=true)

Your personal media hub for movies , TV shows and Books organized, tracked and ready .
## Features


- save progress so u can continue when u last left off
- in the mpv player added some keybinds : "L" lock mode "O" resize window and 
hold right click or space for x2 playback speed


- hold and drag a show or movie card to rearrange it or reorganize it

- epubs pdfs and cbzs are supported in the books section (+heilights system)
- well optimized  (no high ram usage or any problem)

## Screenshots

![App Screenshot](https://github.com/RiRiRonRon/RakshaHub/blob/main/screenshot/Capture%20d'%C3%A9cran%202026-08-23%20000121.png?raw=true)
![App Screenshot](https://github.com/RiRiRonRon/RakshaHub/blob/main/screenshot/Capture%20d'%C3%A9cran%202026-08-04%20115956.png?raw=true)
![App Screenshot](https://github.com/RiRiRonRon/RakshaHub/blob/main/screenshot/Capture%20d'%C3%A9cran%202026-08-22%20235435.png?raw=true)
![App Screenshot](https://github.com/RiRiRonRon/RakshaHub/blob/main/screenshot/Capture%20d'%C3%A9cran%202026-08-22ss%20234337.png?raw=true)


## Installation

you  can download the insatller set up from [releases](https://github.com/RiRiRonRon/RakshaHub/releases) or build it from source following the steps

## Building from source

Requirements:
- Qt 6.11+ with MinGW 64-bit
- libmpv-2.dll  (place in libs/mpv/)
- FFmpeg (place ffmpeg.exe next to built exe)
- gsdll64.dll and gswin64c.exe  (place them next to built exe)(only used for pdfs covers u can ignore them if u want )
-


1. Clone the repo
2. change `secrets.example.h` to `secrets.h` (same folder as `library_manager.cpp`) and add your own OMDB API key free at https://www.omdbapi.com/apikey.aspx
3. Open CMakeLists.txt in Qt Creator
4. Build in Release mode
5. Run windeployqt
