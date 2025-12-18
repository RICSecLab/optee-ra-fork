# Veraison Remote Attestation for i.MX 8M Plus

i.MX 8M Plus向けVeraison Remote AttestationアプリケーションのYoctoビルド環境。

CAAM (Cryptographic Acceleration and Assurance Module) を使用したハードウェアベースの鍵保護をサポート。

## ディレクトリ構成

```
container-imx/
├── build-yocto-in-docker.sh    # Dockerを使用したYoctoビルドスクリプト
├── rebuild-optee-os.sh         # OP-TEE OSの再ビルドスクリプト
├── Dockerfile.yocto            # Yoctoビルド用Dockerイメージ
├── meta-veraison-attestation/  # Yoctoレイヤー
│   ├── conf/
│   │   └── layer.conf
│   └── recipes-security/
│       ├── optee/              # OP-TEE CAAM PTA拡張
│       │   ├── optee-os_%.imx.bbappend
│       │   └── files/pta_remote_attestation/
│       └── veraison-attestation/
│           └── veraison-attestation_1.0.bb
└── src/                        # アプリケーションソースコード
    ├── host/                   # ホストアプリケーション (C + Rust FFI)
    └── ta/                     # Trusted Application
```

## 必要環境

- Docker
- 約100GB のディスク空き容量
- 4-8時間のビルド時間（初回）

## ビルド方法

### 基本的な使用方法

```bash
./build-yocto-in-docker.sh
```

### 環境変数

| 変数 | デフォルト | 説明 |
|------|-----------|------|
| `YOCTO_DIR` | `./yocto` | Yoctoソースとビルドディレクトリの場所 |

### 高速ビルド（tmpfs使用）

RAMディスク（/dev/shm）を使用することでビルドを高速化できます：

```bash
YOCTO_DIR=/dev/shm/yocto ./build-yocto-in-docker.sh
```

**注意**: tmpfsでは一部のパッケージ（gdk-pixbuf-native等）のビルドに失敗する場合があります。その場合は通常のファイルシステムを使用してください。

### カスタムディレクトリ

```bash
YOCTO_DIR=/path/to/yocto ./build-yocto-in-docker.sh
```

## 出力

ビルド成功後、以下の場所にイメージが生成されます：

```
${YOCTO_DIR}/build/tmp/deploy/images/imx8mpevk/core-image-minimal-imx8mpevk.rootfs.wic.zst
```

## SDカードへの書き込み

```bash
cd ${YOCTO_DIR}/build/tmp/deploy/images/imx8mpevk/
zstd -d core-image-minimal-imx8mpevk.rootfs.wic.zst
sudo dd if=core-image-minimal-imx8mpevk.rootfs.wic of=/dev/sdX bs=4M status=progress && sync
```

**注意**: `/dev/sdX` は実際のSDカードデバイスに置き換えてください。

## コンポーネント

### PTA (Pseudo Trusted Application)

OP-TEE OS内で動作するCAAM対応のRemote Attestation PTA：
- CAAM Black Keyによる秘密鍵の保護
- ECDSA P-256署名
- Evidence生成（CBOR/COSE形式）

### TA (Trusted Application)

セキュアワールドで動作するTrusted Application：
- PTAとのインターフェース
- ホストアプリケーションからの呼び出し処理

### Host Application

ノーマルワールドで動作するホストアプリケーション：
- Rust FFIライブラリ（Veraisonクライアント、CBOR処理）
- TAとの通信
- Veraisonサービスへのリモートアテステーション実行

## CAAM設定

ビルドでは以下のCAAM設定が有効化されます：

- `CFG_NXP_CAAM=y` - CAAM有効化
- `CFG_NXP_CAAM_ECC_DRV=y` - ECC暗号ドライバ
- `CFG_NXP_CAAM_BLOB_DRV=y` - Blobドライバ（Black Key用）

## 使用方法（ターゲットデバイス上）

```bash
# Remote Attestationの実行
optee_remote_attestation --verifier <verifier-url> --nonce <base64-nonce>
```
