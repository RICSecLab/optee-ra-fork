# 日本語解説 OP-TEE Remote Attestation with VERAISON Verification

このドキュメントではQEMUとDockerコンテナを用いた[OP-TEE](https://github.com/OP-TEE/optee_os) Remote Attestation 実行環境の構築と、[VERAISON](https://github.com/veraison) Verification を活用した一連の動作を確認手順を説明します。
OP-TEEはRaspberry Pi 3B+ (Arm Cortex-A TrustZone)でも動作が確認できています。

下図はプロビジョニング(0)、リモートアテステーション(1)-(5)、セキュアコミュニケーション(6)の手順を示しています。
![](OPTEE-RA.png)

## 前提条件

開発物を実行するには以下を満たす環境を準備する必要があります。

* Docker がインストールされていること
* Docker デーモンが稼働していること
* Docker 用に30-40GBのディスクが残っていること(`docker system df`で確認できる。容量が少なければ`docker system prune --volumes --all`で確保を勧める。)
* `jq` がインストールされていること（Ubuntu であれば、`sudo apt-get install jq`）

## 実行方法

以下の 0 から 6 の手順に従い、リモートアテステーションの一連の流れをテストしてください。

i.MX 8M Plus 向け Yocto ビルドと実機でのアテステーションは[手順 8](#8-imx8mp-実機でのアテステーション) を参照してください。QEMU 向けの Attester 手順のみ確認したい場合は `attester/README.md` を参照してください。

### 0. このgithubのクローン
最初にgit cloneによりoptee-raのソースを取り寄せます。
```sh
git clone https://github.com/iisec-suzaki/optee-ra
cd optee-ra
```

### 1. Veraison が提供するサービスの起動

Veraisonのソースをgithubから取り寄せます。
```sh
git clone https://github.com/veraison/services.git
cd services && git checkout 8f5734c && cd ..
```
この際に下記のメッセージがでますが、問題ありません。
```
Note: switching to '8f5734c'.

You are in 'detached HEAD' state. You can look around, make experimental
changes and commit them, and you can discard any commits you make in this
state without impacting any branches by switching back to a branch.

If you want to create a new branch to retain commits you create, you may
do so (now or later) by using -c with the switch command. Example:

  git switch -c <new-branch-name>

Or undo this operation with:

  git switch -

Turn off this advice by setting config variable advice.detachedHead to false

HEAD is now at 8f5734c Yogesh's review comments
```

次にホストマシン上で動作させるサービスを起動します。
以下のコマンドにより、Veraison を起動することができます。起動には時間がかかります。
```sh
make -C services docker-deploy
```

サービスを起動した後、以下のコマンドで、サービスの状態を確認することができます。zsh を使っている場合、`source services/deployments/docker/env.bash` の代わりに `source services/deployments/docker/env.zsh` を実行してください。
```sh
source services/deployments/docker/env.bash
veraison status
```

正常に、Veraison のサービスが起動した場合、以下のような出力が得られます。
```txt
         vts: running
provisioning: running
verification: running
  management: running
    keycloak: running
```

### 2. Provisioning の実行

以下のコマンドで、Verifier に対して、`trust anchor` と `reference value` を登録します。これらの値は Attester から送信された evidence の検証に用いられます。登録する値を変更したい場合は `provisoning/data` 以下のファイルを改変してください。
```sh
# QEMU (default)
./provisoning/run.sh qemu
# i.MX 8M Plus
./provisoning/run.sh imx
```

引数を省略した場合は `qemu` が使われます。

登録された値は以下のコマンドで確認できます。
```sh
veraison stores
```

登録が成功すると、以下のような出力が得られます。これは Verifier に登録された値を示しています。
```txt
TRUST ANCHORS:
--------------
{
  "scheme": "PSA_IOT",
  "type": "trust anchor",
  "subType": "",
  "attributes": {
    "PSA_IOT.hw-model": "RoadRunner",
    "PSA_IOT.hw-vendor": "ACME",
    "PSA_IOT.iak-pub": "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEMKBCTNIcKUSDii11ySs3526iDZ8A\niTo7Tu6KPAqv7D7gS2XpJFbZiItSs3m9+9Ue6GnvHw/GW2ZZaVtszggXIw==\n-----END PUBLIC KEY-----",
    "PSA_IOT.impl-id": "cWVtdS1vcHRlZS1yYS0wMDAwMDAwMDAwMDAwMDAwMDE=",
    "PSA_IOT.inst-id": "AZDHHoAwT5jWVWpALAWTszqArL0I5K/5xAKfbhfhA5lR"
  }
}

ENDORSEMENTS:
-------------
{
  "scheme": "PSA_IOT",
  "type": "reference value",
  "subType": "PSA_IOT.sw-component",
  "attributes": {
    "PSA_IOT.hw-model": "RoadRunner",
    "PSA_IOT.hw-vendor": "ACME",
    "PSA_IOT.impl-id": "cWVtdS1vcHRlZS1yYS0wMDAwMDAwMDAwMDAwMDAwMDE=",
    "PSA_IOT.measurement-desc": "sha-256",
    "PSA_IOT.measurement-type": "ARoT",
    "PSA_IOT.measurement-value": "MbgFqjT4jfR+fK1O4YyQtZUYD0nhXh7GfhM0EmR6tgc=",
    "PSA_IOT.signer-id": "rLsRx+TaIXIFUjzkzhokWuGiOa48a/2eeHH35di66Gs="
  }
}
```

`TRUST ANCHORS` は Attester から送信された evidence の署名を検証するためのデータを示しています。Attester から送信される evidence には `impl-id` と `inst-id` が含まれており、Verifier は受信した evidence をどの `TRUST ANCHOR` で検証するかを識別するためにそれらを用います。Verifier は識別された署名鍵（公開鍵）を用いて、CBOR(COSE) 形式の evidence を検証します。
* `iak-pub`: evidence の署名を検証するための公開鍵（本プロジェクトでは ECDSA w/ SHA256 アルゴリズムが署名に用いられます。これは生成する evidence の形式が CBOR(COSE) であり、その仕様 [RFC 8152](https://datatracker.ietf.org/doc/html/rfc8152#section-8.1) に準拠したものです。）
* `impl-id`: Attester の ID
* `inst-id`: 公開鍵の ID

`ENDORSEMENTS` は Attester から送信された evidence の内容を検証するためのデータ（オラクル）を示しています。Attester から送信される evidence には `impl-id` と `measurment-value` が含まれており、Verifier は受信した evidence をどの `ENDORSEMENT` と比較するかを識別するために `impl-id` を用います。Verifier は識別された `ENDORSEMENT` に記録されている `measurement-value` と受信した evidence に含まれる `measurement-value` を比較して、コードハッシュ値の正しさを検証します。その他の項目やより詳細な情報は仕様 [Arm's Platform Security Architecture (PSA) Attestation Token](https://datatracker.ietf.org/doc/draft-tschofenig-rats-psa-token/) を確認してください。
* `impl-id`: Attester の ID
* `measurement-value`:　コードハッシュ値（本プロジェクトでは PTA を呼び出す TA コードの SHA256）

### 3. Relying Party の起動

次に、Relying Party を実行するためのコンテナを起動し、アプリケーションを実行します。Relying Party は、Attester からのリクエストを受け、Attester と Verifier の間の通信を仲介します。また、Verifier からアテステーション結果を受信すると、それをログに出力します。
```sh
./relying_party/container/start.sh
```

以下のコマンドで Relying Party のログは確認できます。
```sh
docker logs relying-party-service
```

正常に起動すると、以下のような出力が得られます。
```txt
go build -o rp main.go
./rp
2024/02/22 05:17:54 Relying party is starting...
```

### 4. Attester の起動

次に、アテステーションリクエストを送信する Attester を起動します。コンテナ上の QEMU の上で Attester を動作させる環境を準備しています。以下の 5 つの手順に従い、コンテナの起動し、 Verifier との通信プログラムを実行してください。

#### 4.1. コンテナの起動

以下のコマンドを実行すると、Docker コンテナ上に入ります。注意として、初期実行時にはイメージのビルドに時間がかかるため、コンテナが起動するのに数十分かかることがあります。
```sh
./attester/container/start.sh
```

#### 4.2. normal world のターミナルを開く

手順 4.1. でコンテナを起動したターミナルとは別のターミナルを開き、以下を実行してください。QEMU 上で実行する Attester の normal world に接続する用のターミナルが準備されます。
```sh
./attester/container/launch_soc_term.sh normal
```

#### 4.3. secure world のターミナルを開く

手順 4.1. と 4.2. で使用したターミナルとは別のターミナルを開き、以下を実行してください。QEMU 上で実行する Attester の secure world に接続する用のターミナルが準備されます。
```sh
./attester/container/launch_soc_term.sh secure
```

#### 4.4. ユーザが追加した CA/TA のビルドと、QEMU の起動とログイン

手順 4.1. で起動したターミナルで以下コマンドを実行してください。ユーザが追加した CA/TA を再ビルドし、QEMU を起動します。PTA は OP-TEE 本体の `veraison_attestation` PTA で、本プロジェクトのパッチ群（[attester/container-imx/meta-veraison-attestation/recipes-security/optee/optee-os/](attester/container-imx/meta-veraison-attestation/recipes-security/optee/optee-os/) 以下。本家には https://github.com/OP-TEE/optee_os/pull/8109 として提出）はコンテナイメージのビルド時に当てられています。必要なのはビルドフラグだけです。
```sh
make -C ${OPTEE_DIR}/build run CFG_VERAISON_ATTESTATION_PTA=y -j
```

QEMU が立ち上がったら、c を入力します。
```sh
(qemu) c
```

次に、normal world のターミナル（2. で起動したターミナル）上で、`test` ユーザでログインします。
```sh
buildroot login: test
```

#### 4.5. プログラムの実行

プログラムは normal world のターミナル上で起動します。
開発した evidence 生成プログラムを実行する場合は、以下のコマンドで実行できます。
```sh
optee_remote_attestation
```

正しく実行できた場合、以下のような出力が noromal world のターミナルで得られます。
```txt
Opened new Veraison client session at http://relying-party-service:8087/challenge-response/v1/session/82e2edd9-0d53-11f1-9b92-393833646162

Number of media types accepted: 9
	application/psa-attestation-token
	application/eat+cwt; eat_profile="tag:psacertified.org,2023:psa#tfm"
	application/vnd.parallaxsecond.key-attestation.tpm
	application/eat-cwt; profile="http://arm.com/psa/2.0.0"
	application/eat-collection; profile="http://arm.com/CCA-SSD/1.0.0"
	application/vnd.parallaxsecond.key-attestation.cca
	application/eat+cwt; eat_profile="tag:psacertified.org,2019:psa#legacy"
	application/vnd.enacttrust.tpm-evidence
	application/pem-certificate-chain

Nonce size: 32 bytes
Nonce: [0xc4, 0x69, 0xd9, 0x7, 0x4, 0x87, 0xac, 0x71, 0x90, 0x30, 0x1f, 0x6d, 0x17, 0xbb, 0x62, 0x7c, 0x95, 0x8, 0x4f, 0x49, 0x2a, 0x5, 0x83, 0x1b, 0x3d, 0xde, 0x2a, 0x8f, 0x89, 0xd5, 0x41, 0x3c]

Completed opening the session.


Invoke TA.
Invoked TA successfully.


Received evidence of CBOR (COSE) format from PTA.

CBOR(COSE) size: 310
CBOR(COSE): d28443a10126a058eba71901097818687474703a2f2f61726d2e636f6d2f7073612f322e302e3019095a1a5f7cd29d19095b19300019095c582071656d752d6f707465652d72612d30303030303030303030303030303030303119095f81a3016441526f540258204237fb23701092316805005b86b2ab60f5ffb681e19e67d97a29e0939a04ea30055820acbb11c7e4da217205523ce4ce1a245ae1a239ae3c6bfd9e7871f7e5d8bae86b0a5820c469d9070487ac7190301f6d17bb627c95084f492a05831b3dde2a8f89d5413c19010058210190c71e80304f98d6556a402c0593b33a80acbd08e4aff9c4029f6e17e1039951584032935e3ebc3c2c052b7ec31fd8f22c4be5ad43cf21960db1de916d8a967bbae0bc1fa2dd110329cb3edeef1919beb74018fe7fbae99fab1743e024ec5f698150


Supplying the generated evidence to the server.

Received the attestation result from the server.

Disposing client session.

Completed sending the evidence and receiving the attestation result.
```

### 5. 検証の実行と結果の確認

もう一つターミナルを開き、以下のコマンドでrelying party のターミナルのログを確認できます。
```sh
docker logs relying-party-service
```

アテステーション結果は `ear.status` の欄に記載されており、`affirming` であれば正しいアテステーション結果が得られたことを意味しています。

`ear.status` が `warning` で `executables not recognized` のようなログが出る場合は、古いエンドースメントが残っています。`env.bash` を source したシェルでストアをクリアし、provisioning をやり直してください（実機は `imx` を指定します）。

```sh
veraison clear-stores
./provisoning/run.sh qemu
```
```txt
Attestation result:
[claims-set]
{
    "ear.verifier-id": {
        "build": "N/A",
        "developer": "Veraison Project"
    },
    "eat_profile": "tag:github.com,2023:veraison/ear",
    "submods": {
        "PSA_IOT": {
            "ear.appraisal-policy-id": "policy:PSA_IOT",
            "ear.status": "affirming",
            "ear.trustworthiness-vector": {
                "configuration": 0,
                "executables": 2,
                "file-system": 0,
                "hardware": 2,
                "instance-identity": 2,
                "runtime-opaque": 2,
                "sourced-data": 0,
                "storage-opaque": 2
            },
            "ear.veraison.annotated-evidence": {
                "eat-profile": "http://arm.com/psa/2.0.0",
                "psa-client-id": 1602015901,
                "psa-implementation-id": "cWVtdS1vcHRlZS1yYS0wMDAwMDAwMDAwMDAwMDAwMDE=",
                "psa-instance-id": "AZDHHoAwT5jWVWpALAWTszqArL0I5K/5xAKfbhfhA5lR",
                "psa-security-lifecycle": 12288,
                "psa-software-components": [
                    {
                        "measurement-type": "ARoT",
                        "measurement-value": "Qjf7I3AQkjFoBQBbhrKrYPX/toHhnmfZeingk5oE6jA=",
                        "signer-id": "rLsRx+TaIXIFUjzkzhokWuGiOa48a/2eeHH35di66Gs="
                    },
                    {
                        "measurement-type": "PRoT",
                        "measurement-value": "GEXvNatCVOIWXWTrDuDroAeUoynz136EUnSEp42BGhM=",
                        "signer-id": "rLsRx+TaIXIFUjzkzhokWuGiOa48a/2eeHH35di66Gs=",
                        "version": "4.6.0"
                    }
                ]
            }
        }
    }
}
[trustworthiness vectors]
submod(PSA_IOT):
Instance Identity [affirming]: The Attesting Environment is recognized, and the associated instance of the Attester is not known to be compromised.
Configuration [none]: The Evidence received is insufficient to make a conclusion.
Executables [affirming]: Only a recognized genuine set of approved executables, scripts, files, and/or objects have been loaded during and after the boot process.
File System [none]: The Evidence received is insufficient to make a conclusion.
Hardware [affirming]: An Attester has passed its hardware and/or firmware verifications needed to demonstrate that these are genuine/supported.
Runtime Opaque [affirming]: the Attester's executing Target Environment and Attesting Environments are encrypted and within Trusted Execution Environment(s) opaque to the operating system, virtual machine manager, and peer applications.
Storage Opaque [affirming]: the Attester encrypts all secrets in persistent storage via using keys which are never visible outside an HSM or the Trusted Execution Environment hardware.
Sourced Data [none]: The Evidence received is insufficient to make a conclusion.
```

> **OP-TEE OS のバージョン(`PRoT` ソフトウェアコンポーネント)。** TA を測定する
> `ARoT` コンポーネントに加え、evidence には `measurement-type` が `"PRoT"` の
> 2 つ目のソフトウェアコンポーネントが含まれ、その `version` フィールドに
> **OP-TEE OS のバージョン**(例: `"4.6.0"`)が入ります(上記参照)。
>
> `measurement-value` はコアの不変領域(`.text` + `.rodata`)の実行時ハッシュで、
> **ビルド固有**の値です(OP-TEE は `core_v_str` にビルド日時を埋め込み、それが
> 測定対象の `.rodata` に含まれるため、OP-TEE OS を再ビルドすると変わります)。
> そのため `PRoT` の参照値は**リリースビルドごとに登録(更新)**します。参照値は
> イメージを起動したりコアのログレベルを上げたりしなくても、ビルド成果物から
> [provisoning/compute-prot-refval.py](provisoning/compute-prot-refval.py) で
> オフライン計算できます:
>
> ```bash
> ./provisoning/compute-prot-refval.py path/to/tee.elf
> ```
>
> 出力された `sha-256;...` ダイジェストを
> `provisoning/data/comid-psa-refval-*.json` の `PRoT` measurement に記載して
> プロビジョニングしてください。参照値を登録すると検証側は `PRoT` も他の
> コンポーネントと同様に照合します: 一致するビルドなら `ear.status` は
> `affirming` のまま、異なる OP-TEE ビルド(またはコード改変)は `warning` として
> 検出されます。参照値を登録しない場合は、このコンポーネントは照合対象外として
> 無視され、`ear.status` は他のコンポーネントだけで決まります。なお Yocto
> ビルドは再現可能(`SOURCE_DATE_EPOCH` 設定済み・クリーンビルドでビルドカウンタ
> がリセット)なので、同一ソースの再ビルドで参照値は変わりません。QEMU コンテナ
> ビルドは非再現のため、ビルドごとに値の取り直しが必要です。`ARoT` の参照値も
> 同様に、初回実行からの取得ではなく
> [provisoning/compute-arot-refval.py](provisoning/compute-arot-refval.py)
> (`.ta` または TA の ELF を渡す)でビルド成果物からオフライン計算できます。

### 6. 異なる TA からアテステーションリクエストを送信するシナリオ

ここまでで、リモートアテステーションが成功する一連の流れを確認しました。次に、異なる TA からアテステーションリクエストを送った際に失敗するシナリオと、新たな endorsment を登録することでアテステーションが成功されるシナリオを確認します。

#### 6.1. 登録されていない TA からのアテステーションリクエスト

はじめに、登録されていない TA から PTA にリクエストを送り、リモートアテステーションが失敗する流れを確認します。例えば、TA を別の implementation ID でビルドすると（[`attester/remote_attestation/ta/include/remote_attestation_ta.h`](attester/remote_attestation/ta/include/remote_attestation_ta.h) の `RA_IMPLEMENTATION_ID`。ビルド時に `CFG_RA_IMPLEMENTATION_ID` で指定します）、TA のコードハッシュと implementation ID が変わり、PTA が生成する CBOR(COSE) evidence の内容が変わります。これにより、provisioning されているデータと異なるので、アテステーションが失敗するはずです。
```sh
make -C ${OPTEE_DIR}/build run CFG_VERAISON_ATTESTATION_PTA=y CFG_RA_IMPLEMENTATION_ID=qemu-optee-ra-000000000000000002 -j
```

実際に、ビルドし直した TA でアテステーションリクエストを送信してみます。手順 4.4. で QEMU を起動したターミナルで `ctrl+c` をして、一度 QEMU を終了します。その後、手順 4.4 のコマンドの代わりに上のコマンドを実行し、TA の再ビルド・QEMU の再起動をします。

その後、手順 4.5. に従い、アテステーションリクエストを送り、手順 5. に従い結果を確認すると、以下のようなアテステーション結果が得られます。`"ear.status": "contraindicated"` になっており、アテステーションに失敗していることがわかります。 
```txt
2024/02/22 05:36:19 Received request: POST /challenge-response/v1/newSession?nonceSize=32
2024/02/22 05:36:19 Received response: 201 Created
2024/02/22 05:36:20 Received request: POST /challenge-response/v1/session/4e2f256e-d144-11ee-9588-623338313838
2024/02/22 05:36:20 Received response: 200 OK
2024/02/22 05:36:20 Attestation result: >> "/tmp/3383640462.jwt" signature successfully verified using "pkey.json"
[claims-set]
{
    "ear.verifier-id": {
        "build": "commit-b50b67d",
        "developer": "Veraison Project"
    },
    "eat_nonce": "J5um9vO4NYv8hYqPXUWsYuuWgW0TLV0qoZGQk3EnIUg=",
    "eat_profile": "tag:github.com,2023:veraison/ear",
    "iat": 1708580180,
    "submods": {
        "PSA_IOT": {
            "ear.appraisal-policy-id": "policy:PSA_IOT",
            "ear.status": "contraindicated",
            "ear.trustworthiness-vector": {
                "configuration": 99,
                "executables": 99,
                "file-system": 99,
                "hardware": 99,
                "instance-identity": 99,
                "runtime-opaque": 99,
                "sourced-data": 99,
                "storage-opaque": 99
            },
            "ear.veraison.policy-claims": {
                "problem": "no trust anchor for evidence"
            }
        }
    }
}
[trustworthiness vectors]
submod(PSA_IOT):
Instance Identity [contraindicated]: Cryptographic validation of the Evidence has failed.
Configuration [contraindicated]: Cryptographic validation of the Evidence has failed.
Executables [contraindicated]: Cryptographic validation of the Evidence has failed.
File System [contraindicated]: Cryptographic validation of the Evidence has failed.
Hardware [contraindicated]: Cryptographic validation of the Evidence has failed.
Runtime Opaque [contraindicated]: Cryptographic validation of the Evidence has failed.
Storage Opaque [contraindicated]: Cryptographic validation of the Evidence has failed.
Sourced Data [contraindicated]: Cryptographic validation of the Evidence has failed.
```

#### 6.2. provisioning で新たな TA を登録する

はじめに、新たな TA のコードハッシュ値を確認します。推奨は、ビルド成果物から
[provisoning/compute-arot-refval.py](provisoning/compute-arot-refval.py)
で**オフライン計算**する方法です(新しい TA の `.ta` ファイルまたは ELF を渡します。実行は不要です):

```bash
./provisoning/compute-arot-refval.py path/to/<uuid>.ta
```

別の方法として、デバッグビルドでは実行時の出力からも取得できます。PTA に
evidence 生成リクエストを送ると、secure terminal にコードハッシュ値がデバッグ用に
出力されます。具体的には以下のような一行があり、`gw9v98IV8ozl5nHpsMwl9W5nGGC0bzAYMPShwvff0vY=`
がコードハッシュ値を base64 エンコードした値です。(注意: i.MX の本番ビルドは
コアログレベルが 0 のためこのデバッグ出力は得られません。実機ではオフライン計算を
使ってください。)
```txt
D/TC:? 0 cmd_get_cbor_evidence:82 b64_measurement_value: gw9v98IV8ozl5nHpsMwl9W5nGGC0bzAYMPShwvff0vY=
```

この値を provisioning で登録します。そのためには、[`provisoning/data/comid-psa-refval-qemu.json`](provisoning/data/comid-psa-refval-qemu.json) の `digests` の欄を以下のように書き換えてください（実機の場合は `provisoning/data/comid-psa-refval-imx.json` を使います）。
また、implementation ID も `qemu-optee-ra-000000000000000002` に変更しているため、[`provisoning/data/comid-psa-refval-qemu.json`](provisoning/data/comid-psa-refval-qemu.json) と [`provisoning/data/comid-psa-ta-qemu.json`](provisoning/data/comid-psa-ta-qemu.json) の `psa.impl-id` の欄を以下のように書き換えてください。注意しとして、`psa.impl-id` の欄は implementation ID を base64 エンコードした値を登録する必要があります。例えば、`echo -n "qemu-optee-ra-000000000000000002" | base64` のようなコマンドで計算できます。
```txt
diff --git a/provisoning/data/comid-psa-refval-qemu.json b/provisoning/data/comid-psa-refval-qemu.json
index fd7965a..db675c1 100644
--- a/provisoning/data/comid-psa-refval-qemu.json
+++ b/provisoning/data/comid-psa-refval-qemu.json
@@ -22,7 +22,7 @@
           "class": {
             "id": {
               "type": "psa.impl-id",
-              "value": "cWVtdS1vcHRlZS1yYS0wMDAwMDAwMDAwMDAwMDAwMDE="
+              "value": "cWVtdS1vcHRlZS1yYS0wMDAwMDAwMDAwMDAwMDAwMDI="
             },
             "vendor": "ACME",
             "model": "RoadRunner"
@@ -39,7 +39,7 @@
             },
             "value": {
               "digests": [
-                "sha-256;MbgFqjT4jfR+fK1O4YyQtZUYD0nhXh7GfhM0EmR6tgc="
+                "sha-256;gw9v98IV8ozl5nHpsMwl9W5nGGC0bzAYMPShwvff0vY="
               ]
             }
           }
diff --git a/provisoning/data/comid-psa-ta-qemu.json b/provisoning/data/comid-psa-ta-qemu.json
--- a/provisoning/data/comid-psa-ta-qemu.json
+++ b/provisoning/data/comid-psa-ta-qemu.json
           "class": {
             "id": {
               "type": "psa.impl-id",
-              "value": "cWVtdS1vcHRlZS1yYS0wMDAwMDAwMDAwMDAwMDAwMDE="
+              "value": "cWVtdS1vcHRlZS1yYS0wMDAwMDAwMDAwMDAwMDAwMDI="
             },
             "vendor": "ACME",
             "model": "RoadRunner"
```

その後、手順 2. に従い、provisioning を再実行します。その結果、以下のように二つの `TRUST ANCHORS` と `ENDORSEMENTS` が登録され、`PSA_IOT.impl-id` と `PSA_IOT.measurement-value` の部分のみが異なることを確認できます。
```json
TRUST ANCHORS:
--------------
{
  "scheme": "PSA_IOT",
  "type": "trust anchor",
  "subType": "",
  "attributes": {
    "PSA_IOT.hw-model": "RoadRunner",
    "PSA_IOT.hw-vendor": "ACME",
    "PSA_IOT.iak-pub": "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEMKBCTNIcKUSDii11ySs3526iDZ8A\niTo7Tu6KPAqv7D7gS2XpJFbZiItSs3m9+9Ue6GnvHw/GW2ZZaVtszggXIw==\n-----END PUBLIC KEY-----",
    "PSA_IOT.impl-id": "cWVtdS1vcHRlZS1yYS0wMDAwMDAwMDAwMDAwMDAwMDE=",
    "PSA_IOT.inst-id": "AZDHHoAwT5jWVWpALAWTszqArL0I5K/5xAKfbhfhA5lR"
  }
}
{
  "scheme": "PSA_IOT",
  "type": "trust anchor",
  "subType": "",
  "attributes": {
    "PSA_IOT.hw-model": "RoadRunner",
    "PSA_IOT.hw-vendor": "ACME",
    "PSA_IOT.iak-pub": "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEMKBCTNIcKUSDii11ySs3526iDZ8A\niTo7Tu6KPAqv7D7gS2XpJFbZiItSs3m9+9Ue6GnvHw/GW2ZZaVtszggXIw==\n-----END PUBLIC KEY-----",
    "PSA_IOT.impl-id": "cWVtdS1vcHRlZS1yYS0wMDAwMDAwMDAwMDAwMDAwMDI=",
    "PSA_IOT.inst-id": "AZDHHoAwT5jWVWpALAWTszqArL0I5K/5xAKfbhfhA5lR"
  }
}

ENDORSEMENTS:
-------------
{
  "scheme": "PSA_IOT",
  "type": "reference value",
  "subType": "PSA_IOT.sw-component",
  "attributes": {
    "PSA_IOT.hw-model": "RoadRunner",
    "PSA_IOT.hw-vendor": "ACME",
    "PSA_IOT.impl-id": "cWVtdS1vcHRlZS1yYS0wMDAwMDAwMDAwMDAwMDAwMDE=",
    "PSA_IOT.measurement-desc": "sha-256",
    "PSA_IOT.measurement-type": "ARoT",
    "PSA_IOT.measurement-value": "MbgFqjT4jfR+fK1O4YyQtZUYD0nhXh7GfhM0EmR6tgc=",
    "PSA_IOT.signer-id": "rLsRx+TaIXIFUjzkzhokWuGiOa48a/2eeHH35di66Gs="
  }
}
{
  "scheme": "PSA_IOT",
  "type": "reference value",
  "subType": "PSA_IOT.sw-component",
  "attributes": {
    "PSA_IOT.hw-model": "RoadRunner",
    "PSA_IOT.hw-vendor": "ACME",
    "PSA_IOT.impl-id": "cWVtdS1vcHRlZS1yYS0wMDAwMDAwMDAwMDAwMDAwMDI=",
    "PSA_IOT.measurement-desc": "sha-256",
    "PSA_IOT.measurement-type": "ARoT",
    "PSA_IOT.measurement-value": "gw9v98IV8ozl5nHpsMwl9W5nGGC0bzAYMPShwvff0vY=",
    "PSA_IOT.signer-id": "rLsRx+TaIXIFUjzkzhokWuGiOa48a/2eeHH35di66Gs="
  }
}
```

その後、手順 4.5. に従いアテステーションリクエストを送り、手順 5. に従い結果を確認すると、以下のようなアテステーション結果が得られます。`"ear.status": "affirming"` になっており、アテステーションに成功していることがわかります。
```json
[claims-set]
{
    "ear.verifier-id": {
        "build": "N/A",
        "developer": "Veraison Project"
    },
    "eat_nonce": "yBULiGEcBq8wtk5xwRikzPZt1GAV5n8L0nXgPY03jHo=",
    "eat_profile": "tag:github.com,2023:veraison/ear",
    "iat": 1708581024,
    "submods": {
        "PSA_IOT": {
            "ear.appraisal-policy-id": "policy:PSA_IOT",
            "ear.status": "affirming",
            "ear.trustworthiness-vector": {
                "configuration": 0,
                "executables": 2,
                "file-system": 0,
                "hardware": 2,
                "instance-identity": 2,
                "runtime-opaque": 2,
                "sourced-data": 0,
                "storage-opaque": 2
            },
            "ear.veraison.annotated-evidence": {
                "eat-profile": "http://arm.com/psa/2.0.0",
                "psa-client-id": 403236456,
                "psa-implementation-id": "cWVtdS1vcHRlZS1yYS0wMDAwMDAwMDAwMDAwMDAwMDI=",
                "psa-instance-id": "AZDHHoAwT5jWVWpALAWTszqArL0I5K/5xAKfbhfhA5lR",
                "psa-nonce": "yBULiGEcBq8wtk5xwRikzPZt1GAV5n8L0nXgPY03jHo=",
                "psa-security-lifecycle": 12288,
                "psa-software-components": [
                    {
                        "measurement-type": "ARoT",
                        "measurement-value": "gw9v98IV8ozl5nHpsMwl9W5nGGC0bzAYMPShwvff0vY=",
                        "signer-id": "rLsRx+TaIXIFUjzkzhokWuGiOa48a/2eeHH35di66Gs="
                    }
                ]
            }
        }
    }
}
```

### 7. クリーンアップ

以下のコマンドでこのテストのために立ち上げたコンテナなどを停止することができます。

```sh
make -C services really-clean
docker stop relying-party-service
docker network rm veraison-net
```

### 8. i.MX8MP 実機でのアテステーション

本セクションでは i.MX8MP EVK 実機上で PSA Remote Attestation を実行し、
Veraison で検証する手順を説明します。evidence はデバイス上で PTA が自分で
保持する鍵で署名されるため、Verifier にはデバイスごとに 1 つの trust anchor が
必要です。鍵の用意の仕方として、PTA が初回利用時に生成する方法と、別の場所で
生成した鍵をインポートする方法の 2 つを扱います。

#### 8.0 i.MX8MP Yocto ビルドと SD 書き込み

必要環境:
- Docker
- 約100GB の空き容量（初回）
- 4-8時間のビルド時間（初回）

フルイメージのビルド（リポジトリ直下から）:
```bash
cd attester/container-imx
./yocto.sh
```

tmpfs を使って高速化する場合:
```bash
YOCTO_DIR=/dev/shm/yocto ./yocto.sh
```

注意: tmpfs では一部パッケージ（例: `gdk-pixbuf-native`）のビルドが失敗する場合があります。その場合は通常のファイルシステムを使用してください。

部分的な再ビルド:
```bash
./yocto.sh optee-os
./yocto.sh veraison-attestation
```

注意: i.MX8MP は SD 先頭の `imx-boot` に埋め込まれた `tee.bin` を使用します。`./yocto.sh optee-os` は `imx-boot` の再ビルドまで実行しますが、SD 書き込み用の WIC を更新するには `./yocto.sh`（full）で再パッケージしてください。

イメージの OP-TEE OS は NXP の 4.6.0.imx ツリーに、本家の `veraison_attestation`
PTA と本プロジェクトが OP-TEE に提出した一連の変更
（https://github.com/OP-TEE/optee_os/pull/8109 とその続編）を
[`optee-os_%.imx.bbappend`](attester/container-imx/meta-veraison-attestation/recipes-security/optee/optee-os_%.imx.bbappend)
がパッチとして当てたものです。PTA は `CFG_VERAISON_ATTESTATION_PTA_TEST_KEY=n`
（署名鍵はビルド時の鍵ではなく secure storage から）と
`CFG_VERAISON_ATTESTATION_PTA_PLATFORM_CLAIMS=y`（lifecycle と boot signer を
fuse から）でビルドされ、`local.conf` の `CFG_NXP_CAAM=y CFG_NXP_CAAM_ECC_DRV=y`
により鍵は CAAM ブラックキーになります。

出力イメージ:
```
${YOCTO_DIR}/build/tmp/deploy/images/imx8mpevk/core-image-minimal-imx8mpevk.rootfs.wic.zst
```

SD カードへの書き込み:
```bash
cd ${YOCTO_DIR}/build/tmp/deploy/images/imx8mpevk/
zstd -d core-image-minimal-imx8mpevk.rootfs.wic.zst
sudo dd if=core-image-minimal-imx8mpevk.rootfs.wic of=/dev/sdX bs=4M status=progress && sync
```

`/dev/sdX` は実際の SD カードデバイスに置き換えてください。

#### 前提条件

| 項目 | 詳細 |
|------|------|
| ボード | i.MX8MP EVK |
| ビルド | `core-image-minimal` (Yocto + アテステーション PTA 入りの OP-TEE + veraison-attestation の TA とクライアント) |
| Veraison | Docker デプロイメント (`services/deployments/docker/`) |
| ネットワーク | デバイスと Veraison ホストが IP 到達可能 |
| SD カード | imx-boot と rootfs の両方を含む WIC イメージ |

> **重要**: i.MX8MP は SD カード先頭の imx-boot 内の `tee.bin` を使用します。
> rootfs 上の `/usr/lib/firmware/tee.bin` を更新しても Secure World には
> 反映されません。PTA のコード変更を反映するには **imx-boot を含む WIC イメージ全体**
> を再フラッシュする必要があります。

デバイス側で Veraison ホストを `/etc/hosts` に追加:
```bash
echo "<Veraison_Host_IP> relying-party-service" >> /etc/hosts
```

Veraison ホスト側でサービスを起動:
```bash
services/deployments/docker/veraison start
source services/deployments/docker/env.bash
```

#### 署名鍵

PTA は鍵が最初に必要になったとき（最初の evidence 生成、または `--get-pubkey`）
に ECDSA P-256 鍵を生成し、自身の private secure storage に保存します。保存先は、
マシンフィーチャ `optee-ftpm` がある場合は eMMC の RPMB
（[docs/ftpm-imx8mp-j.md](docs/ftpm-imx8mp-j.md) 参照）、ない場合は rootfs 上の
`/var/lib/tee` 配下の REE FS（`tee-supplicant` が提供）です。CAAM ECC ドライバを
使うビルドでは、保存されるオブジェクトは CAAM ブラックキーの blob です。秘密鍵が
平文で存在するのは CAAM の内部だけで、blob はデバイスに（Close 後はその secure
state にも）結び付きます（[docs/secure-boot-imx8mp-j.md](docs/secure-boot-imx8mp-j.md)
参照）。

鍵は PTA の外に出ず、置き換えられることもありません。すべての TA の evidence が
この鍵で署名されます。クライアントからできるのは次の 2 つだけです。

- 公開鍵と、そこから導出される instance-id を表示する
  （`optee_remote_attestation --get-pubkey`）
- PTA がまだ鍵を持たない間に、別の場所で生成した鍵を入れる
  （`optee_remote_attestation --import-key`）。Verifier が鍵を事前に知っている
  provisioning フロー向けで、インポートした鍵も同じように CAAM でラップされます。
  鍵がある状態でのインポートは `0xffff0003`（`TEE_ERROR_ACCESS_CONFLICT`）で
  拒否されます。

鍵を作り直すには、鍵オブジェクトを消して TEE を再起動します（8.3 参照）。

#### Provisioning ヘルパー: Instance ID と PEM 公開鍵の計算

`instance-id = 0x01 || SHA-256(0x04 || PubX || PubY)` で、`--get-pubkey` が hex で
表示します。trust anchor には base64 の instance-id と PEM の公開鍵が必要です:

```bash
python3 -c "import base64; print(base64.b64encode(bytes.fromhex('<InstanceID hex>')).decode())"
```

```bash
python3 -c "
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import serialization
pub_x = bytes.fromhex('<PubX hex>')
pub_y = bytes.fromhex('<PubY hex>')
pub_numbers = ec.EllipticCurvePublicNumbers(
    x=int.from_bytes(pub_x, 'big'),
    y=int.from_bytes(pub_y, 'big'),
    curve=ec.SECP256R1()
)
pub_key = pub_numbers.public_key()
pem = pub_key.public_bytes(
    serialization.Encoding.PEM,
    serialization.PublicFormat.SubjectPublicKeyInfo
).decode()
print(pem.strip())
"
```

計算した値で `provisoning/data/comid-psa-ta-imx.json` の `instance` と
`verification-keys` を更新してください。参照値（`comid-psa-refval-imx.json`）は
鍵に依存しません（8.4 参照）。

#### 8.1 シナリオ A: デバイス上で生成した鍵

**手順 1 — 公開鍵の読み出し**（デバイス側）。鍵のないデバイスではここで鍵が
生成されます:
```bash
optee_remote_attestation --get-pubkey
```

出力例:
```
PubX(hex): 9c2676e987bbfab13909276c8682b911079385db43af214130cd2ed7702cc9ee
PubY(hex): 9634ad954d8d6242b09cb3d657674199cd4bc0ec0956e131386e183f9a64dce9
InstanceID(hex): 016536dc438322ac02304783260fb224306bb933fcd351aeb6185217e241f9c3be
```

以後の呼び出しでは、電源を切った後も同じ値が返ります。

**手順 2 — Provisioning**（ホスト側）: PEM 公開鍵と base64 の instance-id を
計算し（上記ヘルパー参照）、`comid-psa-ta-imx.json` を更新後:
```bash
services/deployments/docker/veraison clear-stores
./provisoning/run.sh imx
```

**手順 3 — Attestation 実行**（デバイス側）:
```bash
optee_remote_attestation
```

期待される結果: `"ear.status": "affirming"`

#### 8.2 シナリオ B: 別の場所で生成した鍵（インポート）

PTA がまだ鍵を持たないデバイス（新しいイメージ、または 8.3 の後）向けです。
鍵ペアをホストで生成し、公開鍵を登録し、秘密鍵を一度だけ入れます:

```bash
openssl ecparam -name prime256v1 -genkey -noout -out iak.pem
openssl ec -in iak.pem -noout -text   # priv: が d、pub: が 04 || PubX || PubY
```

**手順 1 — インポート**（デバイス側）:
```bash
optee_remote_attestation --import-key <d hex> --pubx-hex <PubX hex> --puby-hex <PubY hex>
```

出力例:
```
Importing the signing key into the PTA...
PubX(hex): ...
PubY(hex): ...
InstanceID(hex): ...
Key import completed.
```

PTA は秘密鍵が公開鍵と一致することを確認してから CAAM でラップし、平文の鍵は
残しません。

**手順 2 — Provisioning** と **手順 3 — Attestation 実行** はシナリオ A と同じです。

#### 8.3 鍵を作り直す

PTA は一度読み込んだ鍵をキャッシュするため、オブジェクトを消した後は TEE の
再起動が必要です:

- REE FS に保存している場合（`optee-ftpm` なし）: デバイスで
  `rm -rf /var/lib/tee/*` を実行してから再起動します（ディレクトリ自体は
  残してください。`teesuppl` ユーザの所有で、`tee-supplicant` は作り直せません）。
  すべての TA の secure storage が消えます。
- RPMB に保存している場合（`optee-ftpm` あり）: Linux からは消せません。
  `CFG_RPMB_RESET_FS=y` で OP-TEE を一度ビルドして起動時に RPMB ファイルシステムを
  消去し（fTPM の状態も消えます）、その後このオプションなしで再びビルドします。

再起動後、次の `--get-pubkey` または evidence 生成で新しい鍵が生成されるか、
`--import-key` で鍵を入れられます。8.1 と同様に登録してください。デバイスを
Close（Secure Boot）する前に生成した鍵も Close をまたいでは使えません。CAAM の
blob は secure state に結び付くためで、鍵は Close 後に生成（またはインポート）し、
Close 前に生成していた場合は作り直してください。

#### 8.4 evidence の内容

| クレーム | i.MX8MP ビルドでの値 |
|----------|----------------------|
| `psa-implementation-id` | `imx8mp-optee-ra-0000000000000001`（TA レシピの `CFG_RA_IMPLEMENTATION_ID`） |
| `psa-instance-id` | デバイスの鍵の `0x01 \|\| SHA-256(0x04 \|\| PubX \|\| PubY)` |
| `psa-client-id` | 呼び出し元 TA の UUID から導出（1 から 2^31-1） |
| `psa-security-lifecycle` | fuse から: Close 済みなら `0x3000`（secured）、SRK fuse 書き込み済みで未 Close なら `0x2000`（PSA RoT provisioning）、未書き込みなら `0x1000`（assembly and test）、field return 後は `0x6000`（decommissioned） |
| `ARoT` コンポーネント | TA の読み出し専用メモリの SHA-256。signer-id は TA 署名鍵（modulus と exponent）の SHA-256 で、NXP デフォルトの `keys/default_ta.pem` では `ty+NnL9yl7UUSDqhuDTDxU3PCkoIzSVVkHx4g07/xlk=` |
| `PRoT` コンポーネント | OP-TEE コアの `.text` と `.rodata` の SHA-256、version は OP-TEE のバージョン文字列。signer-id は eFuse 順の SRK ハッシュ（boot signer）で、研究室の SRK では `IPNu4+wT0O2kan7wdvMTGWduHBpp0BeE/uSSn/6Bc2A=` |

measurement value はビルド成果物から
[provisoning/compute-arot-refval.py](provisoning/compute-arot-refval.py)
（`.ta`）と
[provisoning/compute-prot-refval.py](provisoning/compute-prot-refval.py)
（`tee.elf`）でオフライン計算します（[provisoning/README-j.md](provisoning/README-j.md)
参照）。PRoT の値は OP-TEE をビルドするたびに変わります。

## 性能計測ログ (RA_PERF)

クライアントと TA は、アテステーション 1 回の処理時間と PTA 呼び出しの所要
時間を機械可読なログ(`RA_PERF|<layer>|<event>|<duration_us>|key=<keymode>`)
として出力できます。TA 側は `CFG_REMOTE_ATTESTATION_PERF=y` でコンパイルし、
クライアント側は実行時に `optee_remote_attestation --perf`(データ収集用に
`--loop N` / `--no-server` も利用可)で有効化します。ログポイント一覧と計測
プロトコルは [docs/performance-measurement-j.md](docs/performance-measurement-j.md)
を参照してください。同文書の PTA 内のログポイントと CAAM ブラックキー使用
有無の比較は、本家の PTA に置き換える前の本プロジェクトのフォーク PTA で計測
したもので、その計測の記録として残しています。

## i.MX8MP でのファームウェア TPM と IMA

マシンフィーチャ `optee-ftpm` を付けると、イメージに Microsoft のファーム ウェア TPM が OP-TEE の early TA として入り、IMA がカーネル初期化中に見つけ られるよう Linux より先に起動します。fTPM は状態を OP-TEE コア内ドライバ経由 で eMMC の RPMB に保存するため、基板上の eMMC は TEE の専有になり、U-Boot は 実行中のシステムを PCR 10 に計測する IMA の引数をカーネルに渡します。設計、 有効化手順、制約は [docs/ftpm-imx8mp-j.md](docs/ftpm-imx8mp-j.md)、計測リスト が fTPM の保証するものであることを TPM の quote で別マシンから確かめる手順 (基板側 `ima-quote`、検証者側 `attester/ima-quote/`)は [docs/ima-quote-j.md](docs/ima-quote-j.md) を参照してください。

## 謝辞
研究は、JST、CREST、JPMJCR21M3 ([Zero Trust IoT プロジェクト](https://zt-iot.nii.ac.jp/)) の支援を受けたものです。
