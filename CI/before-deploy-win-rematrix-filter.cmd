mkdir C:\projects\rematrix-filter\build\obs-plugins\32bit
mkdir C:\projects\rematrix-filter\build\obs-plugins\64bit
mkdir C:\projects\rematrix-filter\data\obs-plugins\rematrix-filter
robocopy "C:\projects\obs-studio\build32\rundir\RelWithDebInfo\obs-plugins\32bit\" "C:\projects\rematrix-filter\build\obs-plugins\32bit\" "rematrix-filter.*"
robocopy "C:\projects\obs-studio\build64\rundir\RelWithDebInfo\obs-plugins\64bit\" "C:\projects\rematrix-filter\build\obs-plugins\64bit\" "rematrix-filter.*"
robocopy /e "C:\projects\obs-studio\build64\rundir\RelWithDebInfo\data\obs-plugins\rematrix-filter\" "C:\projects\rematrix-filter\data\obs-plugins\rematrix-filter\"

7z a C:\projects\rematrix-filter\build.zip C:\projects\rematrix-filter\build*
