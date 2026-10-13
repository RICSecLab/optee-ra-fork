# OP-TEE Attester

The Attester is a client that sends remote authentication requests to the Relying Party (Verifier). This client has a TEE (Trusted Execution Environment, namely Arm TrustZone) and runs on OP-TEE. The Attester is built on QEMU.

![](../OPTEE-RA.png)

## Execution Method

The Attester is started on QEMU within a container. Follow these five steps to start the container and run the communication program with the Verifier "Varaison".

### 1. Starting the Container

Run the following command to enter the Docker container.

```sh
./container/start.sh
```

### 2. Opening the Normal World Terminal

Open another terminal, separate from the one where you started the container in step 1, and execute the following command. This prepares a terminal to connect to the normal world of the Attester running on QEMU.

```sh
./container/launch_soc_term.sh normal
```

### 3. Opening the Secure World Terminal

Open another terminal, separate from the ones used in steps 1 and 2, and execute the following command. This prepares a terminal to connect to the secure world of the Attester running on QEMU.

```sh
./container/launch_soc_term.sh secure
```

### 4. Building User-Added CA/TA, Starting QEMU, and Logging In

In the terminal started in step 1, execute the following command to rebuild the added application and start QEMU. The PTA is OP-TEE's own `veraison_attestation` PTA; the container image applied this project's patch series to it when it was built (see [`container/Dockerfile`](container/Dockerfile)), so only the build flag is needed.
```sh
make -C ${OPTEE_DIR}/build run CFG_VERAISON_ATTESTATION_PTA=y -j
```

Once QEMU has started, enter `c`.
```sh
(qemu) c
```
Next, login as the `test` user on the normal world terminal (the terminal started in step 2).

```sh
buildroot login: test
```

### 5. Running the Program

The program is started on the normal world terminal.
To run the developed evidence generation program, use the following command.
```sh
optee_remote_attestation
```

## Formatting Method

```sh
clang-format -i -style=file remote_attestation/host/*.c remote_attestation/host/*.h remote_attestation/ta/*.c remote_attestation/ta/*.h remote_attestation/ta/include/*.h
```


<!--
-------------------------------------------------------------------------------------------------------------------
-->
---
---
---
# 日本語解説　OP-TEE Attester

Attester は Relying Party (Verifier) に対して、リモート認証のリクエストを送るクライアントです。このクライアントはTEE(Trusted Execution Environment, 具体的にはArm TrustZone)を持ち、 OP-TEE を実行します。現在のAttestaerは QEMU の上で構築されます。

![](../OPTEE-RA.png)

## 実行方法

Attester はコンテナ上の QEMU の上で起動されます。以下の 5 つの手順に従い、コンテナの起動し、 Verifier "Varaison"との通信プログラムを実行してください。

### 1. コンテナの起動

以下のコマンドを実行すると、Docker コンテナ上に入ります。
```sh
./container/start.sh
```

### 2. normal world のターミナルを開く

手順 1. でコンテナを起動したターミナルとは別のターミナルを開き、以下を実行してください。QEMU 上で実行する Attester の normal world に接続する用のターミナルが準備されます。
```sh
./container/launch_soc_term.sh normal
```

### 3. secure world のターミナルを開く

手順 1. と 2. で使用したターミナルとは別のターミナルを開き、以下を実行してください。QEMU 上で実行する Attester の secure world に接続する用のターミナルが準備されます。
```sh
./container/launch_soc_term.sh secure
```

### 4. ユーザが追加した CA/TA のビルドと、QEMU の起動とログイン

手順 1. で起動したターミナルで以下コマンドを実行してください。追加したアプリケーションを再ビルドし、QEMUを起動します。PTA は OP-TEE 本体の `veraison_attestation` PTA で、本プロジェクトのパッチ群はコンテナイメージのビルド時に当てられています（[`container/Dockerfile`](container/Dockerfile) 参照）。必要なのはビルドフラグだけです。
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

### 5. プログラムの実行

プログラムは normal world のターミナル上で起動します。
開発した evidence 生成プログラムを実行する場合は、以下のコマンドで実行できます。
```sh
optee_remote_attestation
```

## フォーマット方法

```sh
clang-format -i -style=file remote_attestation/host/*.c remote_attestation/host/*.h remote_attestation/ta/*.c remote_attestation/ta/*.h remote_attestation/ta/include/*.h
```
