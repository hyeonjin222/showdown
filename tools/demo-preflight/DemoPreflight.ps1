# ShowDown 시연 PC 점검 스크립트
# 게임을 켜기 전에 실행해서 로그인/멀티플레이/AI 대화에 필요한 연결이 되는지 확인합니다.
# 결과만 보여주고, PC 설정은 아무것도 바꾸지 않습니다.

$ErrorActionPreference = 'SilentlyContinue'
$SupabaseHost = 'xfyzrqsbdweckjgxefjr.supabase.co'
$PublishableKey = 'sb_publishable_CFQn2cVa58AV-iRDqq0QfQ_Hy4rARoV'
$Problems = 0

function Write-Ok($Text) { Write-Host "  [OK]   $Text" -ForegroundColor Green }
function Write-Bad($Text, $Fix) {
    Write-Host "  [문제] $Text" -ForegroundColor Red
    if ($Fix) { Write-Host "         -> $Fix" -ForegroundColor Yellow }
    $script:Problems++
}
function Write-Info($Text) { Write-Host "  [참고] $Text" -ForegroundColor Cyan }

Write-Host ''
Write-Host '=== ShowDown 시연 PC 점검 ===' -ForegroundColor White
Write-Host ''

# 1. 인터넷 / 와이파이 웹 인증
Write-Host '1. 인터넷 연결'
$Probe = $null
try { $Probe = Invoke-WebRequest -UseBasicParsing 'http://www.msftconnecttest.com/connecttest.txt' -TimeoutSec 8 } catch {}
if (-not $Probe) {
    Write-Bad '인터넷에 연결되어 있지 않습니다.' '랜선/와이파이 확인, 또는 휴대폰 USB 테더링/핫스팟 연결'
} elseif ($Probe.Content -notmatch 'Microsoft Connect Test') {
    Write-Bad '와이파이 웹 인증(로그인 페이지)이 필요한 네트워크입니다.' '브라우저를 열어 인증하거나 휴대폰 핫스팟 사용'
} else {
    Write-Ok '인터넷 연결됨'

    # 2. PC 시간
    Write-Host '2. PC 시간'
    $ServerTime = [DateTime]::Parse($Probe.Headers['Date']).ToUniversalTime()
    $Skew = [Math]::Abs(([DateTime]::UtcNow - $ServerTime).TotalMinutes)
    if ($Skew -gt 5) {
        Write-Bad ("PC 시간이 실제와 약 {0:N0}분 다릅니다." -f $Skew) "Windows 설정 > 시간 및 언어 > 날짜 및 시간 > '지금 동기화'"
    } else {
        Write-Ok 'PC 시간 정상'
    }
}

# 3. 로그인 서버 (Supabase)
Write-Host '3. 로그인 서버'
$Dns = Resolve-DnsName $SupabaseHost -ErrorAction SilentlyContinue
if (-not $Dns) {
    Write-Bad "로그인 서버 주소($SupabaseHost)를 찾을 수 없습니다." '이 네트워크가 차단 중일 수 있음 -> 휴대폰 핫스팟 사용'
} else {
    try {
        $Health = Invoke-WebRequest -UseBasicParsing "https://$SupabaseHost/auth/v1/health" -Headers @{ apikey = $PublishableKey } -TimeoutSec 10
        if ($Health.StatusCode -eq 200) { Write-Ok '로그인 서버 응답 정상' } else { Write-Bad "로그인 서버 응답 이상 ($($Health.StatusCode))" '서버 담당자에게 Supabase 프로젝트 상태 확인 요청' }
    } catch {
        $Code = $_.Exception.Response.StatusCode.value__
        if ($Code -eq 540 -or $Code -eq 503) {
            Write-Bad '로그인 서버(Supabase 프로젝트)가 일시정지 상태입니다.' 'Supabase 대시보드에서 프로젝트 Restore'
        } else {
            Write-Bad "로그인 서버에 접속할 수 없습니다. ($($_.Exception.Message))" '방화벽/백신 또는 네트워크 차단 -> 핫스팟 사용'
        }
    }
}

# 4. 멀티플레이 (Epic Online Services)
Write-Host '4. 멀티플레이 서버 (EOS)'
try {
    Invoke-WebRequest -UseBasicParsing 'https://api.epicgames.dev/' -TimeoutSec 10 | Out-Null
    Write-Ok 'EOS 서버 연결됨'
} catch {
    if ($_.Exception.Response) { Write-Ok 'EOS 서버 연결됨' }
    else { Write-Bad 'EOS 서버에 접속할 수 없습니다.' '멀티플레이 불가 -> 싱글플레이로 시연하거나 핫스팟 사용' }
}

# 5. AI 대화 (Gemini 무료 키)
Write-Host '5. AI 대화 (Gemini)'
# 프로젝트(tools\demo-preflight)와 패키지(ShowDown\tools\demo-preflight) 모두 키는 두 단계 위 폴더에 있습니다.
$KeyFile = Join-Path $PSScriptRoot '..\..\LLMKey.txt'
$Key = $env:GEMINI_API_KEY
if (-not $Key -and (Test-Path $KeyFile)) { $Key = (Get-Content $KeyFile -Raw).Trim() }
if (-not $Key) {
    Write-Info 'Gemini 키가 없습니다. 보스는 대본 대사로 대답합니다. (게임 진행에는 문제 없음)'
} else {
    try {
        $Models = Invoke-RestMethod 'https://generativelanguage.googleapis.com/v1beta/openai/models' -Headers @{ Authorization = "Bearer $Key" } -TimeoutSec 10
        # 게임이 쓰는 순서(Config/DefaultGame.ini의 GeminiModelCandidates)와 같아야 합니다.
        $Available = $Models.data.id | ForEach-Object { $_ -replace '^models/', '' }
        $Wanted = 'gemma-4-26b-a4b-it', 'gemini-3.1-flash-lite', 'gemini-3.5-flash'
        $Found = $Wanted | Where-Object { $Available -contains $_ }
        if ($Found) { Write-Ok ("Gemini 키 정상. 보스 대화 모델: {0}" -f ($Found -join ' -> ')) }
        else { Write-Bad '키는 정상이지만 게임이 쓰는 모델을 찾을 수 없습니다.' '보스는 대본 대사로 대답합니다.' }
    } catch {
        Write-Bad 'Gemini 키가 거부되었거나 접속할 수 없습니다.' '키를 다시 확인하세요. 실패해도 보스는 대본 대사로 대답합니다.'
    }
}

Write-Host ''
if ($Problems -eq 0) {
    Write-Host '=== 모두 정상입니다. 게임을 실행하세요. ===' -ForegroundColor Green
} else {
    Write-Host "=== 문제 $Problems 건. 위 안내대로 해결하거나, 로그인 화면의 '오프라인으로 시작'을 사용하세요. ===" -ForegroundColor Yellow
}
Write-Host ''
Read-Host '엔터를 누르면 닫힙니다'
