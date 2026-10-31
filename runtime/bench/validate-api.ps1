$ErrorActionPreference = 'Stop'
$url = 'http://127.0.0.1:18932/v1/chat/completions'
$cases = @(
  @{name='arithmetic'; prompt='Answer with only the integer: 17 * 19 = ?'; max=64},
  @{name='reversal'; prompt='Reverse the characters of FORGE. Output only the result.'; max=64},
  @{name='length'; prompt='Write a long detailed story about a lighthouse.'; max=1}
)
foreach ($case in $cases) {
  $body = @{model='local'; messages=@(@{role='user';content=$case.prompt}); temperature=0; seed=42; max_tokens=$case.max; chat_template_kwargs=@{enable_thinking=$false}; cache_prompt=$false} | ConvertTo-Json -Depth 10
  $response = Invoke-RestMethod -Uri $url -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 180
  $response | ConvertTo-Json -Depth 30 | Set-Content -Encoding UTF8 "runtime/bench/validation-$($case.name).json"
  $response | ConvertTo-Json -Depth 30
}
