# TeamShell

## 1. 사전 요구 사항 (Prerequisites)

빌드하기 전에 필요한 라이브러리를 설치해야 합니다. 다음 명령어를 순서대로 입력하세요.

```bash
sudo apt update
sudo apt install -y build-essential libreadline-dev
```

## 2. 빌드 
다음 명령어를 사용하여 프로젝트를 컴파일합니다.

```bash
g++ -std=c++17 -Wall -Wextra -o teamshell teamshell.cpp parser.cpp shell.cpp command.cpp command_factory.cpp builtin_registry.cpp builtins.cpp runtime_state.cpp -lreadline
```

## 3. 실행 (Run)
```bash
./teamshell
```
빌드가 성공적으로 완료되면, 다음 명령어로 프로그램을 실행할 수 있습니다.

