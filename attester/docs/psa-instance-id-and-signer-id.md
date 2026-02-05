# PSA Attestation Token における Instance ID と Signer ID

本ドキュメントでは、PSA Attestation Token に含まれる 2 つの重要なクレーム
**Instance ID** と **Signer ID** について解説する。

## 参照仕様

| 仕様 | URL |
|------|-----|
| PSA Attestation Token | https://datatracker.ietf.org/doc/draft-tschofenig-rats-psa-token/ |
| Entity Attestation Token (EAT) | https://www.rfc-editor.org/rfc/rfc9711.html |
| CBOR Object Signing and Encryption (COSE) | https://www.rfc-editor.org/rfc/rfc9052.html |
| SEC 1: Elliptic Curve Cryptography | https://www.secg.org/sec1-v2.pdf |

## PSA Attestation Token の構造概要

PSA Attestation Token は COSE_Sign1 で署名された CBOR マップである。
本プロジェクトで使用するクレームの全体構造を以下に示す。

```
COSE_Sign1 {
  protected: { alg: ES256 },
  unprotected: {},
  payload: {                           ← CBOR マップ (evidence)
    265: "http://arm.com/psa/2.0.0",   ← Profile Definition
    2394: 1,                            ← Client ID
    2395: 12288,                        ← Security Lifecycle
    2396: <impl_id>,                    ← Implementation ID (32 bytes)
    2399: [                             ← SW Components (配列)
      {
        1: "PRoT",                      ← Measurement Type
        2: <measurement_value>,         ← Measurement Value (SHA-256)
        5: <signer_id>                  ← ★ Signer ID (32 bytes)
      }
    ],
    10: <nonce>,                        ← Nonce
    256: <instance_id>                  ← ★ Instance ID (33 bytes)
  },
  signature: <ECDSA-SHA256 signature>
}
```

---

## Instance ID

### 概要

**Instance ID** (クレームキー `256`) はデバイスの attestation
鍵を一意に識別する識別子である。PSA 仕様では EAT UEID (Universal Entity ID,
RFC 9711 Section 4.2.1) として定義されている。

Verifier (Veraison) は Instance ID を用いて、
受信した evidence に対応する**検証鍵 (trust anchor)** を特定する。

### 計算方法

ECDSA 署名鍵の場合、Instance ID は以下のように計算される:

```
instance_id = 0x01 || SHA-256(0x04 || PubX || PubY)
```

| バイト | 意味 |
|--------|------|
| `0x01` | EAT UEID type: ECDSA |
| `0x04` | SEC 1 uncompressed point prefix |
| `PubX` | 署名鍵の公開鍵 X 座標 (P-256 の場合 32 bytes) |
| `PubY` | 署名鍵の公開鍵 Y 座標 (P-256 の場合 32 bytes) |

結果は 1 + 32 = **33 bytes** となる。

### 本プロジェクトでの実装

PTA の `compute_instance_id()` (`sign.c`) が上記の計算を行う。

```
attester/pta_remote_attestation/remote_attestation/sign.c
  └─ compute_instance_id(pub_x, pub_y, instance_id)
       └─ SHA-256(0x04 || pub_x || pub_y) → instance_id[1..32]
       └─ instance_id[0] = 0x01
```

署名鍵の公開鍵座標 (PubX, PubY) は以下のいずれかから取得される:

1. **外部鍵 (CAAM black key)**: ホストアプリが param[3] で
   `PubX(32) || PubY(32) || key_blob(N)` を渡す
2. **組み込みテスト鍵**: `get_test_key_pubkey()` がソースコードに埋め込まれた
   テスト鍵の公開鍵座標を返す

### Veraison provisioning との対応

Instance ID は Trust Anchor の登録時に使用される。
`provisoning/data/comid-psa-ta.json` の `instance` フィールドが
Instance ID に対応する:

```json
"instance": {
  "type": "ueid",
  "value": "Ac7rrnuJJ6MiflMDz14PH3s0u1Qq1yUKwD+83jbsLxUI"
}
```

この値は `0x01 || SHA-256(0x04 || PubX || PubY)` の Base64 エンコードである。
署名鍵を変更した場合、この値も再計算して更新する必要がある。

---

## Signer ID

### 概要

**Signer ID** (SW Component クレームキー `5`) は、
ファームウェア（ソフトウェアコンポーネント）の**署名者**を識別するハッシュ値である。
PSA Attestation Token 仕様 Section 4.4.1 で定義されている。

Instance ID が「どの attestation 鍵で署名されたか」を示すのに対し、
Signer ID は「どの署名者がファームウェアを認証したか」を示す。

> **重要**: Signer ID は attestation 署名鍵とは無関係である。
> attestation 鍵を CAAM black key に変更しても Signer ID は変わらない。

### 計算方法

```
signer_id = SHA-256(firmware_signing_authority_public_key)
```

ファームウェアのセキュアブート署名に使用された公開鍵の SHA-256 ハッシュである。

結果は **32 bytes** となる。

### 本プロジェクトでの実装

現時点ではハードコードされた値を使用している (`remote_attestation.c`):

```c
#define SIGNER_ID                                      \
    0xac, 0xbb, 0x11, 0xc7, 0xe4, 0xda, 0x21, 0x72,    \
    ...
```

> **FIXME**: セキュアブート統合時に、実際のファームウェア署名鍵の
> 公開鍵ハッシュに置き換える必要がある。

### Veraison provisioning との対応

Signer ID は Reference Value の登録時に使用される。
`provisoning/data/comid-psa-refval-imx.json` (または `*-qemu.json`) の
`signer-id` フィールドが Signer ID に対応する:

```json
"key": {
  "type": "psa.refval-id",
  "value": {
    "label": "PRoT",
    "signer-id": "rLsRx+TaIXIFUjzkzhokWuGiOa48a/2eeHH35di66Gs="
  }
}
```

Veraison は evidence の Signer ID + Measurement Type をキーとして
登録済みの reference value (measurement digest) を検索し、
evidence 内の measurement value と照合する。

---

## Instance ID と Signer ID の比較

| | Instance ID | Signer ID |
|---|---|---|
| **クレームキー** | `256` (トップレベル) | `5` (SW Component 内) |
| **サイズ** | 33 bytes | 32 bytes |
| **何を識別するか** | attestation 署名鍵 (デバイス) | ファームウェア署名者 |
| **計算入力** | attestation 鍵の公開鍵 | FW 署名鍵の公開鍵 |
| **計算方法** | `0x01 \|\| SHA-256(0x04 \|\| PubX \|\| PubY)` | `SHA-256(fw_signing_pubkey)` |
| **Veraison 用途** | Trust Anchor (検証鍵) の特定 | Reference Value (期待値) の検索 |
| **鍵変更時の影響** | attestation 鍵変更で変わる | FW 署名鍵変更で変わる |
| **provisioning ファイル** | `comid-psa-ta.json` | `comid-psa-refval-*.json` |

---

## attestation 鍵変更時のワークフロー

CAAM black key など新しい attestation 鍵を使用する場合:

1. `--generate-blackkey` で鍵を生成 (PubX, PubY, BlackKey を取得)
2. PubX, PubY から Instance ID を計算: `0x01 || SHA-256(0x04 || PubX || PubY)`
3. `comid-psa-ta.json` の `instance.value` を新しい Instance ID の Base64 に更新
4. `comid-psa-ta.json` の `verification-keys` を新しい公開鍵の PEM に更新
5. Veraison に再 provisioning
6. `--key-hex <FullKey>` で attestation 実行

Signer ID (`comid-psa-refval-*.json`) はファームウェア署名鍵が
変わらない限り更新不要。

---

## 関連ファイル

| ファイル | 役割 |
|----------|------|
| `attester/pta_remote_attestation/remote_attestation/sign.c` | `compute_instance_id()`, `get_test_key_pubkey()` |
| `attester/pta_remote_attestation/remote_attestation/sign.h` | 上記関数の宣言 |
| `attester/pta_remote_attestation/remote_attestation/remote_attestation.c` | Instance ID の動的計算、Signer ID 定義 |
| `attester/pta_remote_attestation/remote_attestation/cbor.c` | CBOR エンコード (クレームキー 256, 5) |
| `attester/pta_remote_attestation/remote_attestation/cbor.h` | `PSA_INSTANCE_ID`, `PSA_SW_COMPONENT_SIGNER_ID` 定義 |
| `attester/remote_attestation/host/main.c` | `--pubx-hex`, `--puby-hex` オプション |
| `provisoning/data/comid-psa-ta.json` | Trust Anchor (Instance ID + 検証鍵) |
| `provisoning/data/comid-psa-refval-*.json` | Reference Value (Signer ID + digest) |
