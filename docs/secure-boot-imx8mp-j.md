# Secure Boot（i.MX8MP EVK / HABv4 / SD起動）

このドキュメントは i.MX8MP EVK の HABv4 Secure Boot を、
NXPのHABv4手順（SRK/CSF/Authenticate Data）と i.MX8M の
ブートイメージ形式に沿って再現可能に行う手順です。
署名済み `imx-boot` とカーネル `Image` を作り、WIC に反映します。

**ヒューズ書き込みは不可逆**です。Open での検証が完了するまで Close にしないでください。

## 前提

- コマンドはリポジトリのルートから実行
- Yocto: `/dev/shm/yocto`
- CST tarball: `./cst-3.1.0.tgz`
- 出力先（相対）: `../optee-ra-artifacts/secure-boot`
- SD デバイス: `/dev/sdX`（環境に合わせて置換）

## 1) Yocto ビルド（初回のみ）

```bash
cd ./attester/container-imx
YOCTO_DIR=/dev/shm/yocto ./yocto.sh
```

## 2) 署名済み imx-boot と WIC を作成

```bash
OUT_DIR=../optee-ra-artifacts/secure-boot
OUT_WIC="$YOCTO_DIR/build/tmp/deploy/images/imx8mpevk/core-image-minimal-imx8mpevk.rootfs.wic.zst"

./attester/container-imx/secure-boot-imx8mp.sh \
  --yocto-dir /dev/shm/yocto \
  --cst-tar ./cst-3.1.0.tgz \
  --out-dir "$OUT_DIR" \
  --patch-wic "$OUT_WIC"
```

生成物:
- 署名済み imx‑boot
  - `$OUT_DIR/imx-boot-imx8mpevk-sd.bin-flash_evk-signed`
- 署名済み `Image`
  - `$OUT_DIR/img/Image-signed`
- 署名済み imx‑boot（32KB）と署名済み `Image` を反映した WIC
  - `$OUT_DIR/core-image-minimal-imx8mpevk.rootfs.wic.zst`

各ステップを個別に実行することもできます:

```bash
./attester/container-imx/secure-boot-sign-imx-boot.sh \
  --yocto-dir /dev/shm/yocto \
  --cst-tar ./cst-3.1.0.tgz \
  --out-dir "$OUT_DIR"

./attester/container-imx/secure-boot-sign-kernel.sh \
  --yocto-dir /dev/shm/yocto \
  --cst-tar ./cst-3.1.0.tgz \
  --out-dir "$OUT_DIR"

./attester/container-imx/secure-boot-patch-wic.sh \
  --wic "$OUT_WIC" \
  --signed-imx-boot "$OUT_DIR/imx-boot-imx8mpevk-sd.bin-flash_evk-signed" \
  --signed-image "$OUT_DIR/img/Image-signed" \
  --out-dir "$OUT_DIR"
```

## 3) 鍵のバックアップ（必須）

CST が生成した鍵は以下にあります:
`$OUT_DIR/cst`

必ずアーカイブしてください:

```bash
sudo tar -czf "$OUT_DIR/secure-boot-keys.tar.gz" \
  -C "$OUT_DIR" cst
```

## 4) SRK Fuse 値の抽出

```bash
hexdump -e '/4 "0x"' -e '/4 "%X""\n"' \
  "$OUT_DIR/cst/keys/SRK_1_2_3_4_fuses.bin"
```

出力された 8 行を `fuse prog` で使います。

## 5) SD へ書き込み

```bash
cd "$OUT_DIR"
zstd -d core-image-minimal-imx8mpevk.rootfs.wic.zst
sudo dd if=core-image-minimal-imx8mpevk.rootfs.wic of=/dev/sdX bs=4M status=progress conv=fsync
```

`/dev/sdX` は実際のデバイスに置換してください。

## 6) Open での確認（U-Boot）

```text
=> hab_status
```

Open では `Secure boot disabled` のままで OK。

## 7) SRK Fuse 書き込み（U-Boot）

**書き込み前**:

```text
=> fuse read 6 0 4
=> fuse read 7 0 4
```

**SRK 書き込み**（Step 4 の値に置換）:

```text
=> fuse prog 6 0 0x........
=> fuse prog 6 1 0x........
=> fuse prog 6 2 0x........
=> fuse prog 6 3 0x........

=> fuse prog 7 0 0x........
=> fuse prog 7 1 0x........
=> fuse prog 7 2 0x........
=> fuse prog 7 3 0x........
```

**書き込み後**:

```text
=> fuse read 6 0 4
=> fuse read 7 0 4
```

再起動:

```text
=> reset
```

## 8) Close（不可逆）

Open での確認が終わってから実行:

```text
=> fuse prog 1 3 0x2000000
```

再起動後:

```text
=> hab_status
```

`Secure boot enabled` が出れば Close 完了。

## 9) Remote Attestation 確認

`relying-party-service` が引けない場合:

```bash
echo "192.168.8.244 relying-party-service" >> /etc/hosts
```

デバイスの鍵を読み出し（まだ鍵がなければここで PTA が生成します）、Verifier に
登録します（README の 8.1 節）:

```bash
optee_remote_attestation --get-pubkey
```

実行:

```bash
optee_remote_attestation
```

期待値: `ear.status: "affirming"`。

Close 前に生成した鍵は Close をまたいでは使えません。Secure Boot により CAAM の
鍵文脈（MPMR/BKEK）が変わるため、保存されている CAAM blob を開けなくなり、
evidence 生成が失敗します（`TEEC_InvokeCommand failed`）。鍵オブジェクトを消して
TEE を再起動し（README の 8.3 節）、新しい鍵を読み出して登録し直してください。

## 10) デバイスごとの鍵（2台）

PTA が保存する CAAM blob はデバイス固有です。ハードウェア固有のエントロピーから
導出される各デバイスの JDKEK でラップされるため、同じ SRK で Close した 2 台でも
署名鍵は別々になり、Verifier はそれぞれを知っている必要があります。blob は PTA の
外に出ないので、この性質は Verifier 側で確認します。鍵が登録されていないデバイスは
拒否されます。

両方の実機を **同じ SRK** で Close し（手順 7-8）、Device A は手順 9 のとおり
登録しておきます。

### 10.1 Device B を未登録の自身の鍵で実行

**Device B** で:

```bash
optee_remote_attestation --get-pubkey
optee_remote_attestation
```

期待: `ear.status: "contraindicated"`、`"no trust anchor for evidence"`。

Device B は自身の鍵で署名します（fuse、参照値、boot signer は A と同じ）が、
その公開鍵、したがって `psa-instance-id` が trust anchor として登録されていません。

### 10.2 Device B を Verifier に登録して検証

`--get-pubkey` の出力から instance-id と PEM 公開鍵を計算し（README の 8 節の
ヘルパー参照）、Device B 用の provisioning JSON を作成して submit します:

```bash
# Device B の trust anchor をビルド・登録
veraison -- cocli comid create \
    --template provisoning/data/comid-psa-ta-deviceB.json \
    --template provisoning/data/comid-psa-refval-imx.json \
    --output-dir provisoning/data

veraison -- cocli corim create \
    --template provisoning/data/corim-psa.json \
    --comid provisoning/data/comid-psa-refval-imx.cbor \
    --comid provisoning/data/comid-psa-ta-deviceB.cbor \
    --output provisoning/data/psa-endorsements-deviceB.cbor

veraison -- cocli corim submit \
    --corim-file provisoning/data/psa-endorsements-deviceB.cbor \
    --api-server "https://provisioning-service:9443/endorsement-provisioning/v1/submit" \
    --media-type 'application/corim-unsigned+cbor; profile="http://arm.com/psa/iot/1"'
```

**Device B** で再度アテステーションを実行:

```bash
optee_remote_attestation
```

期待: `ear.status: "affirming"`。

### まとめ

| テスト | 結果 | 意味 |
|--------|------|------|
| Device B、鍵未登録 | `no trust anchor` | 各デバイスは自身の CAAM に結び付いた鍵で署名し、その instance-id を Verifier が知らない |
| Device B を登録後 | `affirming` | trust anchor 追加後、アテステーション成功 |
| Close 前に生成した鍵 | evidence 生成失敗 | CAAM blob は secure state に結び付く。Close 後に作り直す |

## 注意事項

- Close は不可逆です。鍵のバックアップは必須。
- 署名済み imx‑boot は WIC の 32KB に埋め込まれます。
- 2台検証では SRK 値を必ず揃える必要があります。
- 各デバイスは固有の CAAM 鍵ペアを生成するため、Verifier にはデバイスごとに
  trust anchor を個別登録する必要があります。
