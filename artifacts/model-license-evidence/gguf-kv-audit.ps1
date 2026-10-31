# GGUF metadata (KV) read-only extraction + template comparison.
# Reads ONLY the metadata section at the top of the GGUF; never streams the full file to a hash.
param(
    [Parameter(Mandatory=$true)][string] $GgufPath,
    [string[]] $CompareRemote = @()   # URLs of HF model APIs whose gguf.chat_template we compare against
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Read-Str([System.IO.BinaryReader] $r, [System.IO.Stream] $s) {
    $len = $r.ReadUInt64()
    $bytes = $r.ReadBytes([int]$len)
    return [System.Text.Encoding]::UTF8.GetString($bytes)
}
function Skip-Value([System.IO.BinaryReader] $r, [System.IO.Stream] $s, [uint32] $type) {
    switch ($type) {
        0 { $s.Position += 1 }       # u8
        1 { $s.Position += 1 }       # i8
        2 { $s.Position += 2 }       # u16
        3 { $s.Position += 2 }       # i16
        4 { $s.Position += 4 }       # u32
        5 { $s.Position += 4 }       # i32
        6 { $s.Position += 4 }       # f32
        7 { $s.Position += 1 }       # bool
        8 { $null = Read-Str $r $s } # string
        9 {                          # array
            $et = $r.ReadUInt32()
            $cnt = $r.ReadUInt64()
            for ($i = 0; $i -lt $cnt; $i++) { Skip-Value $r $s $et }
        }
        10 { $s.Position += 8 }      # u64
        11 { $s.Position += 8 }      # i64
        12 { $s.Position += 8 }      # f64
        default { throw "Unknown GGUF KV type $type" }
    }
}
function Read-Value([System.IO.BinaryReader] $r, [System.IO.Stream] $s, [uint32] $type) {
    switch ($type) {
        8 { return [string] (Read-Str $r $s) }
        0 { return $r.ReadByte() }
        4 { return $r.ReadUInt32() }
        10 { return $r.ReadUInt64() }
        7 { return ($r.ReadByte() -ne 0) }
        default { throw "Type $type conversion not implemented for capture" }
    }
}

$fs = [System.IO.File]::OpenRead($GgufPath)
try {
    $br = [System.IO.BinaryReader]::new($fs)
    $magicBytes = $br.ReadBytes(4)
    $magic = [System.Text.Encoding]::ASCII.GetString($magicBytes)
    if ($magic -ne 'GGUF') { throw "Not a GGUF file (magic=$magic)" }
    $version = $br.ReadUInt32()
    $tensorCount = $br.ReadUInt64()
    $kvCount = $br.ReadUInt64()
    Write-Output ("GGUF magic ok, version={0}, tensor_count={1}, kv_count={2}" -f $version, $tensorCount, $kvCount)

    $kvs = @{}
    for ($i = 0; $i -lt $kvCount; $i++) {
        $key = Read-Str $br $fs
        $type = $br.ReadUInt32()
        if ($key -in @('tokenizer.chat_template', 'general.architecture', 'general.name', 'general.source_url', 'general.source.hugging_face.repository', 'general.quantization_version', 'general.file_type', 'tokenizer.ggml.bos_token_id', 'tokenizer.ggml.eos_token_id')) {
            try { $kvs[$key] = Read-Value $br $fs $type }
            catch { throw ("Capturing key '{0}' failed: {1}" -f $key, $_.Exception.Message) }
        }
        else { Skip-Value $br $fs $type }
    }
    Write-Output '--- selected metadata ---'
    foreach ($k in (@('general.architecture','general.name','general.source_url','general.source.hugging_face.repository','general.file_type','general.quantization_version','tokenizer.ggml.bos_token_id','tokenizer.ggml.eos_token_id'))) {
        if ($kvs.ContainsKey($k)) { Write-Output ("{0} = {1}" -f $k, $kvs[$k]) }
    }
    if (-not $kvs.ContainsKey('tokenizer.chat_template')) { throw 'tokenizer.chat_template not found in metadata' }
    $tpl = [string]$kvs['tokenizer.chat_template']

    # hash of the template (UTF-8 bytes) + SSTI marker scan of ALL kv strings captured + template
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $tplBytes = [System.Text.Encoding]::UTF8.GetBytes($tpl)
    $tplHash = ($sha.ComputeHash($tplBytes) | ForEach-Object { $_.ToString('x2') }) -join ''
    Write-Output ('template_length_chars = ' + $tpl.Length)
    Write-Output ('template_utf8_len = ' + $tplBytes.Length)
    Write-Output ('template_sha256(UTF8) = ' + $tplHash)
    $markers = @('__class__','__subclasses__','__builtins__','subprocess','popen','os.system','eval(','exec(','__import__','<script','invoke-expression','start-process')
    $hits = $markers | Where-Object { $tpl.Contains($_) }
    Write-Output ('ssti_markers_found = ' + $(if ($hits) { $hits -join ', ' } else { 'NONE' }))
    $tplFile = Join-Path ([System.IO.Path]::GetTempPath()) ('gguf-chat-template-' + [System.IO.Path]::GetFileNameWithoutExtension($GgufPath) + '.jinja')
    [System.IO.File]::WriteAllText($tplFile, $tpl, [System.Text.UTF8Encoding]::new($false))
    Write-Output ('template_saved_to = ' + $tplFile)

    # optional comparison with remote HF APIs
    foreach ($url in $CompareRemote) {
        try {
            $repo = Invoke-RestMethod -Uri $url -TimeoutSec 60
            $remoteTemplate = $repo.gguf.chat_template
            if ([string]::IsNullOrEmpty($remoteTemplate)) { Write-Output ("REMOTE {0}: no gguf.chat_template present" -f $url); continue }
            $rBytes = [System.Text.Encoding]::UTF8.GetBytes($remoteTemplate)
            $rHash = ($sha.ComputeHash($rBytes) | ForEach-Object { $_.ToString('x2') }) -join ''
            $equal = ($rBytes.Length -eq $tplBytes.Length) -and (-not (Compare-Object $tplBytes $rBytes -SyncWindow 0))
            Write-Output ("REMOTE {0}: repo_sha={1} len={2} sha256={3} EQUAL_LOCAL={4}" -f $url, $repo.sha, $rBytes.Length, $rHash, $equal)
        } catch {
            Write-Output ("REMOTE {0}: FAILED: {1}" -f $url, $_.Exception.Message)
        }
    }
}
finally { $fs.Dispose() }
