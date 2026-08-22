# usage: .\source\sprt.ps1 new.exe old.exe [rounds] [tc]
param(
    [string]$NewExe,
    [string]$OldExe,
    [int]$Rounds = 1000,
    [string]$Tc = "10+0.1"
)
& resources\fastchess\fastchess.exe `
    -engine cmd=$NewExe name=new -engine cmd=$OldExe name=old `
    -each tc=$Tc option.Hash=256 `
    -openings file=resources\fastchess\UHO.pgn format=pgn order=random plies=16 `
    -sprt elo0=0 elo1=10 alpha=0.05 beta=0.05 `
    -rounds $Rounds -repeat -concurrency 10 -recover -ratinginterval 25
