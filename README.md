# my-study
```
print("Hello World")
```
AWS EC2에 Java 백엔드를 올리고, C++ 클라이언트가 HTTP로 데이터를 전송해 점수를 기록하는 초소형 통신 프로젝트입니다. 깃허브에 /backend와 /client로 나누어 올리기.
프로젝트 아키텍처 및 역할
 * Java (Spring Boot): AWS EC2에서 실행되며 C++ 클라이언트가 보낸 점수를 받아 저장하고 조회하는 REST API 서버
 * C++ (클라이언트): 임의의 점수를 생성해 AWS 서버로 HTTP POST 요청을 보내는 프로그램
 * AWS (EC2): Java 서버를 외부에서 접근할 수 있도록 호스팅하는 클라우드 서버
1. Java 백엔드 핵심 코드 (ScoreController.java)
package com.example.demo;

import org.springframework.web.bind.annotation.*;

@RestController
@RequestMapping("/api")
public class ScoreController {
    private int maxScore = 0;

    @PostMapping("/score")
    public String updateScore(@RequestParam int score) {
        if (score > maxScore) {
            maxScore = score;
        }
        return "Score received: " + score + " | Current Max: " + maxScore;
    }

    @GetMapping("/score")
    public int getMaxScore() {
        return maxScore;
    }
}

2. C++ 클라이언트 핵심 코드 (main.cpp)
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <string>

// 참고: 실제 HTTP 전송 시 libcurl 등의 라이브러리 활용
int main() {
    std::srand(std::time(nullptr));
    int myScore = std::rand() % 1000 + 1;
    
    std::cout << "Generated Score: " << myScore << std::endl;
    
    std::string command = "curl -X POST \"http://<AWS_EC2_IP>:8080/api/score?score=" + std::to_string(myScore) + "\"";
    std::system(command.c_str());
    
    return 0;
}

3. 깃허브 리포지토리 구조
/cpp-java-aws-project
├── backend/                  # Java Spring Boot 소스 코드
│   ├── src/
│   └── build.gradle (또는 pom.xml)
├── client/                   # C++ 클라이언트 소스 코드
│   └── main.cpp
└── README.md                 # 프로젝트 설명 및 아키텍처 구조도

4. AWS 배포 및 실행 순서
 * AWS EC2 (Ubuntu) 인스턴스를 생성하고 Java(JDK) 환경을 세팅합니다.
 * Java 프로젝트를 빌드(jar 파일 생성)하여 EC2로 전송한 뒤 백그라운드 실행합니다 (java -jar app.jar &).
 * EC2 **보안 그룹(Security Group)**에서 8080 포트를 인바운드 규칙으로 열어줍니다.
 * 로컬 PC나 C++ 프로그램에서 http://<EC2-퍼블릭-IP>:8080/api/score로 요청을 날려 통신을 확인합니다.
