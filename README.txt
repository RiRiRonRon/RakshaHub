.______          ___       __  ___      _______. __    __       ___          __    __   __    __  .______   
|   _  \        /   \     |  |/  /     /       ||  |  |  |     /   \        |  |  |  | |  |  |  | |   _  \  
|  |_)  |      /  ^  \    |  '  /     |   (----`|  |__|  |    /  ^  \       |  |__|  | |  |  |  | |  |_)  | 
|      /      /  /_\  \   |    <       \   \    |   __   |   /  /_\  \      |   __   | |  |  |  | |   _  <  
|  |\  \----./  _____  \  |  .  \  .----)   |   |  |  |  |  /  _____  \     |  |  |  | |  `--'  | |  |_)  | 
| _| `._____/__/     \__\ |__|\__\ |_______/    |__|  |__| /__/     \__\    |__|  |__|  \______/  |______/  
                                                                                                            


Your personal media hub for movies , TV shows and Books organized, tracked, and ready .


                                                                                                            
## Building from source

Requirements:
- Qt 6.11+ with MinGW 64-bit
- libmpv (place in libs/mpv/)
- FFmpeg (place ffmpeg.exe next to built exe)

1. Clone the repo
2. change `secrets.example.h` to `secrets.h` (same folder as `library_manager.cpp`) and add your own OMDB API key free at https://www.omdbapi.com/apikey.aspx
3. Open CMakeLists.txt in Qt Creator
4. Build in Release mode
5. Run windeployqt

 
![App Screenshot]https://github.com/RiRiRonRon/RakshaHub/blob/main/screenshot/Capture%20d'%C3%A9cran%202026-08-23%20000121.png?raw=true)

