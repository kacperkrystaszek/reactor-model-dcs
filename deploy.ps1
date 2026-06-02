$ErrorActionPreference = "Stop"

$PiFeed = "pi@192.168.70.2"
$PiCoolant = "pi@192.168.70.3"
$SourceDir = "..\reactor-model-dcs"

Write-Host "==============================================" -ForegroundColor Cyan
Write-Host " ROZPOCZYNAM DEPLOYMENT KONTROLERÓW" -ForegroundColor Cyan
Write-Host "==============================================" -ForegroundColor Cyan

# 1. Kopiowanie plików na Feed Controller (.2)
Write-Host "`n[1/4] Kopiowanie plików na Feed Controller ($PiFeed)..." -ForegroundColor Yellow
scp -r "$SourceDir\include" "$PiFeed`:~/tocompile"
scp -r "$SourceDir\src" "$PiFeed`:~/tocompile"

# 2. Kopiowanie plików na Coolant Controller (.3)
Write-Host "`n[2/4] Kopiowanie plików na Coolant Controller ($PiCoolant)..." -ForegroundColor Yellow
scp -r "$SourceDir\include" "$PiCoolant`:~/tocompile"
scp -r "$SourceDir\src" "$PiCoolant`:~/tocompile"

# 3. Kompilacja Feed Controller (.2)
Write-Host "`n[3/4] Kompilacja i instalacja Feed Controller na $PiFeed..." -ForegroundColor Magenta
ssh $PiFeed "cd ~/tocompile && g++ -O2 -Wall -Iinclude -I. -DDISABLE_RT_LOGGING src/feed_main.cpp src/config/ConfigLoader.cpp src/messages/MessageConstructor.cpp src/UDPSocket.cpp src/Controller.cpp -o controller -lpthread -lrt && mv ~/tocompile/controller ~/dcs/controller && chmod +x ~/dcs/controller && sudo chmod +x ~/dcs/controller"

# 4. Kompilacja Coolant Controller (.3) - Poprawiony adres IP na .3
Write-Host "`n[4/4] Kompilacja i instalacja Coolant Controller na $PiCoolant..." -ForegroundColor Magenta
ssh $PiCoolant "cd ~/tocompile && g++ -O2 -Wall -Iinclude -I. -DDISABLE_RT_LOGGING src/coolant_main.cpp src/config/ConfigLoader.cpp src/messages/MessageConstructor.cpp src/UDPSocket.cpp src/Controller.cpp -o controller -lpthread -lrt && mv ~/tocompile/controller ~/dcs/controller && chmod +x ~/dcs/controller && sudo chmod +x ~/dcs/controller"

Write-Host "`n==============================================" -ForegroundColor Green
Write-Host " DEPLOYMENT ZAKOŃCZONY SUKCESEM!" -ForegroundColor Green
Write-Host "==============================================" -ForegroundColor Green