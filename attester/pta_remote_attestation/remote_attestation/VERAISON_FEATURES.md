# Veraison Attestation 機能追加

このドキュメントは、Veraison Attestation PTAに追加された3つの新機能について説明します。

## 追加された機能

### 1. 外部鍵からBlack Keyへの変換（プロビジョニング）
**コマンド**: `PTA_VERAISON_ATTESTATION_CONVERT_TO_BLACKKEY` (0x1)

この機能は、外部から提供された平文のECC秘密鍵を受け取り、CAAMのBlack Key形式に変換します。

**パラメータ**:
- `[in] memref[0]`: 平文秘密鍵 d (P-256の場合32バイト)
- `[out] memref[1]`: シリアライズされたBlack Key (サイズ問い合わせ可能)
- `[out] memref[2]`: 公開鍵のX座標 (32バイト)
- `[out] memref[3]`: 公開鍵のY座標 (32バイト)

**使用例**:
```c
// 平文秘密鍵をBlack Keyに変換
uint8_t plain_private_key[32] = { /* 秘密鍵データ */ };
uint8_t black_key[256];  // Black Keyバッファ
uint8_t pub_x[32], pub_y[32];
size_t black_key_size = sizeof(black_key);

TEE_Param params[4];
params[0].memref.buffer = plain_private_key;
params[0].memref.size = 32;
params[1].memref.buffer = black_key;
params[1].memref.size = black_key_size;
params[2].memref.buffer = pub_x;
params[2].memref.size = 32;
params[3].memref.buffer = pub_y;
params[3].memref.size = 32;

// PTAを呼び出してBlack Keyに変換
```

### 2. ゼロからBlack Keyを生成（プロビジョニング）
**コマンド**: `PTA_VERAISON_ATTESTATION_GENERATE_ECC_KEYPAIR` (0x0) ※既存機能

この機能は既に実装されていました。新規にCAAM-backedのECC P-256鍵ペアを生成し、秘密鍵をBlack Key形式で出力します。

**パラメータ**:
- `[out] memref[0]`: シリアライズされたBlack Key (秘密鍵d)
- `[out] memref[1]`: 公開鍵のX座標 (32バイト)
- `[out] memref[2]`: 公開鍵のY座標 (32バイト)

### 3. 鍵なしでCBOR Evidenceを作成
**コマンド**: `PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE` (0x2) の`params[3]`省略時

この機能は、`PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE`コマンドで鍵パラメータ（`params[3]`）を省略することで利用できます。その場合、組み込みのテスト鍵を使用してCBOR形式のエビデンスを生成します。

**パラメータ**:
- `[in] memref[0]`: Nonce
- `[out] memref[1]`: 出力バッファ (CBOR/COSE形式のエビデンス)
- `[in] memref[2]`: Implementation ID
- `params[3]`: 省略（NONE）

**注意**: 鍵なしで動作させるには、`CFG_VERAISON_ATTESTATION_PTA_TEST_KEY`が有効になっている必要があります。

## CAAM依存性

機能1と機能2は、NXP CAAMハードウェアセキュリティモジュールが必要です。CAAMが利用できない場合、これらの機能は`TEE_ERROR_NOT_SUPPORTED`を返します。

CAAMを有効にするには、ビルド設定で以下のフラグを設定してください：
```
CFG_NXP_CAAM_ECC_DRV=y
```

## Black Keyの利点

Black Keyは以下の利点を提供します：
1. **ハードウェア保護**: 鍵はCAAMによって暗号化され、デバイス固有
2. **非エクスポート可能**: 平文の秘密鍵は抽出できない
3. **セキュア**: ハードウェアレベルでの鍵管理

## 使用フロー

### プロビジョニングフロー
1. **既存の鍵がある場合**: `PTA_VERAISON_ATTESTATION_CONVERT_TO_BLACKKEY`を使用
2. **新規鍵を生成する場合**: `PTA_VERAISON_ATTESTATION_GENERATE_ECC_KEYPAIR`を使用

### アテステーションフロー
`PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE`を使用:
1. **Black Keyがある場合**: params[3]にBlack Keyを渡す
2. **鍵がない場合**: params[3]を省略（テスト鍵使用）

## セキュリティ考慮事項

- Black Keyはデバイス固有であり、他のデバイスでは使用できません
- テスト鍵モード（機能3）は開発/テスト環境でのみ使用してください
- 本番環境では適切にプロビジョニングされたBlack Keyを使用することを推奨します
