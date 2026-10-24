#pragma once
#include <string>

namespace g_CheatData {
	namespace VTable {
		namespace D3D12 {
			inline int Present = 8;
			inline int ExecuteCommandLists = 10;
			inline int ResizeBuffers = 13;
		}

		namespace AActor {
			inline int Tick = 470;
		}

		namespace AShooterPlayerCameraManager {
			// ASA CL 833800 / Steam build 26003405 (2026-10-24).
			// UpdateCamera is at RVA 0x66A5070 and remains in client vtable slot 472.
			inline int UpdateCamera = 472;
		}

		namespace UMovementComponent {
			inline int PhysicsRotation = 289; // verified client 93.34: 318 is ApplyVelocityBraking
		}

		namespace UGameViewportClient {
			inline int PostRender = 122;
		}

		namespace UConsole {
			inline int OutputTextLine = 99;
		}
	}

	namespace Signature {
		namespace AShooterPlayerController {
			// 48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 48 8D 6C 24 ? 48 81 EC ? ? ? ? 80 65 ? ? 33 DB 0F

			// 应该不再需要这个东西了，没有用处，只是视觉效果
			inline std::string ClientChatMessage = "OUTDATED";
     	}

		namespace APrimalPlayerController {
			inline std::string ClientNotifyReconnected = "OUTDATED";

			// v93 uses FindUFunction at runtime; the old 453-byte signature no longer matches.
			inline std::string ClientNotifyRespawned = "OUTDATED";
		}

		namespace AActor {
			// push rbp | push rbx | push rsi | push rdi | push r12 | push r13 | push r14 | push r15 | lea rbp, [rsp+offset] | sub rsp, alloc | movaps [rsp+offset], xmm11
			// 无法在服务器工作，需要找到ASA重写的TakeDamage，或者UI Hit Damage，idk
			inline std::string TakeDamage = "OUTDATED";

			// 2026/10/24, ASA CL 833800 / Steam build 26003405 (unique at RVA 0x4CC6E00)
			// 470
			inline std::string Tick = "40 53 48 83 EC ? 0F B6 81 ? ? ? ? 48 8B D9 A8 ? 75 ? A8 ? 74";
		}

		namespace UNetConnection {
			// 2026/10/24, ASA CL 833800 / Steam build 26003405 (unique at RVA 0x46B0C60)
			// 也没啥用，退出服务器的时候会触发一次。
			// 不敢相信这个特征码能存活这么长时间
			inline std::string HandleDisconnect = "? ? ? ? ? ? ? 55 57 48 8D 68 A1 48 81 EC D8 00 00 00 48 89 58 E8 48 8D 55 E7 48 89 70 E0";
		}

		namespace UMovementComponent {
			// 48 89 5C 24 ? 48 89 7C 24 ? 55 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 44 0F 29 BC 24 过期了
			// 2026/10/24, ASA CL 833800 / Steam build 26003405 (unique at RVA 0x4147BC0)
			// mov rax, rsp | push rbx | sub rsp, alloc | movaps [rax+offset], xmm6 | mov rbx, rcx | movaps [rax+offset], xmm7 | movaps
			// 48 8B C4 53 48 81 EC 00 01 00 00 0F 29 70 E8 48 8B D9 0F 29 78 D8 0F 28

			// The active hook resolves by the unique signature; slot 289 is a historical class-specific clue.
			inline std::string PhysicsRotation = "48 89 5C 24 18 48 89 7C 24 20 55 48 8D AC 24 10 FF FF FF 48 81 EC F0 01 00 00 44 0F 29 BC 24 50 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 40 F6 81 08 03 00 00 18";
		}

		/*
		namespace UWorld {
			// 7FF7A935DA20 World Tick?
			// 7FF7A935DC40 World Post Tick?
			// 2026/4/3 @zetsr
			// ? ? ? ? ? 57 48 83 EC ? 48 8B 42 ? 49 8B E8 48 89 5C 24 ? 4C 8B E9 48 89 74 24 ? 48 8B 2026/4/3 过期了

			// 2026/4/3 @zetsr
			// push rbp push r14 push r15 sub rsp, offset mov rax, [rdx + offset] mov eax, [rdx + offset] mov [rsp + offset], rbx mov ebp, ecx mov [rsp + offset], rsi mov rax, [rdx]

			// 2026/4/26 @zetsr & @Lynneren
			// push rbx; push rbp; push rsi; push rdi; push r12; push r14; push r15; sub rsp, sz; mov rax, [cookie]; xor rax, rsp; mov [rsp+sz], rax; mov r14, rcx; mov r15, r8; lea rcx, [rsp+30h]
			// 40 53 55 56 57 41 54 41 56 41 57 48 81 EC ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 ?? ?? ?? ?? 4C 8B F1 4D 8B F8 48 8D 4C 24 30 48

			// 2026/5/2 @zetsr
			// mov r1x, [mem] | push rbp | push r14 | push r15 | sub rsp, alloc | mov rax, [rip+offset]
			// 4C 8B ? 55 41 56 41 57 48 81 EC ? ? 00 00 48 8B 05

			// 2026/5/11 @zetsr
			// push rbp; push r13; push r15; sub rsp, 0x40; mov rax, [rdx+0x20]; mov r11, [r8]
			// 40 55 41 55 41 57 48 83 EC ? 48 8B ? ? 49 8B

			// 2026/5/20 @zetsr
			// mov r11, rsp | push rbp | push r14 | push r15 | sub rsp, alloc | mov rax, [rip+offset]
			// 4C 8B DC 55 41 56 41 57 48 81 EC 80 08 00 00 48 8B 05 CA 9B D4 08

			// 2026/5/23 @zetsr
			// push rbx | push rbp | push rsi | push rdi | push r12 | push r14 | push r15 | sub rsp, alloc | mov rax, [rip+offset] | xor rax, rsp | mov [rsp+offset], rax | mov r14, rcx | mov r15, r8 | lea rcx, [rsp+offset]
			// 40 53 55 56 57 41 54 41 56 41 57 48 81 EC 60 08 00 00 48 8B 05 27 B8 AC 08 48 33 C4 48 89 84 24 50 08 00 00 4C 8B F1 4D 8B F8 48 8D 4C 24 30

			// 2026/6/6 @zetsr
			// mov r11, rsp | push rbp | push r14 | push r15 | sub rsp, alloc | mov rax
			// 4C 8B DC 55 41 56 41 57 48 81 EC 80 08 00 00 48 8B 05

			// 2026/6/10 @zetsr
			// push rbx | push rbp | push rsi | push rdi | push r12 | push r14 | push r15 | sub rsp, alloc | mov rax, [rip+offset] | xor rax, rsp | mov [rsp+offset], rax | mov r14, rcx
			std::string Tick = "4C 8B DC 55 41 56 41 57 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 49";
		}
		*/

		namespace UGameViewportClient {
			// https://github.com/vsylva/ark-asa-internal
			// VTable = 122，不知道这个虚表会不会过期，可能还是需要做特征码

			// 8B C2 35 DE C5 F3 59 44 8B C0 69 C0 B1 79 37 9E 41 C1 E8 0D 44 33 C0 41 69 C0 6B CA EB 85
			// mov eax, edx | xor eax, imm32 | mov r8d, eax | imul eax, eax, imm32 | shr r8d, imm8 | xor r8d, eax | imul r8d, r8d, imm32
			
			// 122
			// Steam build 26003405 retains vtable slot 122; no unique AOB is currently needed.
			inline std::string PostRender = "OUTDATED";
		}

		namespace UConsole {
			// 2026/4/3 @zetsr
			// 48 8B C4 ? ? 48 83 EC ? 48 89 58 ? 48 8B DA 2026/4/3 过期了

			// mov [rsp + offset], rbx push rbp push rsi push rdi push r12 push r13 push r14 push r15 mov rax, [rcx] sub rsp, offset movsxd rax, [rdx + offset] xor r14d, r14d mov rax, [rcx]
			// 48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8B ? 48 83 EC ? 48 63 ? ? 45 33 F6 48 8B

			// 2026/5/11 @zetsr
			// mov rax, rsp; push r12; sub rsp, 0x60; mov [rax+0x08], rbx; mov rbx, rdx
			// 48 8B C4 41 54 48 83 EC ? 48 89 58 ? 48 8B ?

			// 2026/5/20 @zetsr
			// mov [rsp+8], rbx | push rbp | push rsi | push rdi | push r12 | push r13 | push r14 | push r15 | mov rbp, rsp | sub rsp, alloc | movsxd rsi, [rdx+offset] | xor r14d, r14d
			// 48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 40 48 63 72 08 45 33 F6

			// 2026/5/23 @zetsr
			// mov rax, rsp | push r12 | sub rsp, alloc | mov [rax+8], rbx | mov rbx, rdx
			// 48 8B C4 41 54 48 83 EC 60 48 89 58 08 48 8B DA

			// 2026/6/6 @zetsr
			// mov [rsp+8], rbx | push rbp | push rsi | push rdi | push r12 | push r13 | push r14 | push r15 | mov rbp, rsp | sub rsp, alloc | movsxd rsi, [rdx+offset]
			// 48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 40 48 63 72 08

			// 2026/6/10 @zetsr
			// mov rax, rsp | push r12 | sub rsp, alloc | mov [rax+8], rbx | mov

			// 2026/10/24, ASA CL 833800 / Steam build 26003405 (unique at RVA 0x4BE4870)
			// 99
			inline std::string OutputTextLine = "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC ? 48 63 72 ? 45 33 F6";
		}
	}
}
