// Owned, serial-validated scalar overrides shared with the regression fixture.
    struct ExpansionPatch {
        ExtensionObject Owner;
        int Offset = -1;
        uint8_t Mask = 0;
        double Original = 0, Applied = 0;
        bool IsDouble = false;
        bool Seen = false;
        double Read(SDK::UObject* O) const {
            auto* P = reinterpret_cast<uint8_t*>(O) + Offset;
            return Mask ? double((*P & Mask) != 0) : IsDouble ? *reinterpret_cast<double*>(P) : *reinterpret_cast<float*>(P);
        }
        void Write(SDK::UObject* O, double Value) const {
            auto* P = reinterpret_cast<uint8_t*>(O) + Offset;
            if (Mask) *P = Value != 0 ? (*P | Mask) : (*P & uint8_t(~Mask));
            else if (IsDouble) *reinterpret_cast<double*>(P) = Value;
            else *reinterpret_cast<float*>(P) = static_cast<float>(Value);
        }
        void Restore() {
            if (auto* O = Owner.Get(); O && Offset >= 0 && Read(O) == Applied) Write(O, Original);
            Owner = {}; Offset = -1; Seen = false;
        }
        void Set(SDK::UObject* O, const char* Name, double Value, bool Multiply = false, const char* Member = nullptr) {
            Seen = true;
            if (!IsUsableObject(O)) { Restore(); return; }
            if (Owner.Get() != O) {
                Restore(); Seen = true; Owner = ExtensionObject(O);
                auto* P = FindReflectedProperty(O->Class, Name);
                int BaseOffset = 0, Bounds = O->Class->Size;
                if (Member) {
                    if (!P || !P->ClassPrivate || P->ClassPrivate->Name.ToString() != "StructProperty" ||
                        P->ArrayDim != 1 || P->Offset < 0 || P->ElementSize <= 0 ||
                        P->ElementSize > Bounds || P->Offset > Bounds - P->ElementSize) return;
                    auto* S = static_cast<SDK::FStructProperty*>(P)->Struct;
                    if (!S || S->Size != P->ElementSize) return;
                    BaseOffset = P->Offset; Bounds = P->ElementSize;
                    P = FindReflectedProperty(S, Member);
                }
                if (!P || !P->ClassPrivate || P->Offset < 0 || P->Offset > 0x100000 ||
                    P->ArrayDim != 1 || Bounds <= 0 || P->Offset >= Bounds) return;
                const auto Type = P->ClassPrivate->Name.ToString();
                Mask = 0; IsDouble = false;
                if (Type == "BoolProperty") {
                    auto* B = static_cast<SDK::FBoolProperty*>(P);
                    if (B->ByteOffset >= 8) return;
                    Mask = B->FieldMask ? B->FieldMask : B->ByteMask;
                    if (!Mask) return;
                    if (P->Offset + B->ByteOffset >= Bounds) return;
                    Offset = BaseOffset + P->Offset + B->ByteOffset;
                }
                else if (Type == "FloatProperty" && P->ElementSize == sizeof(float) &&
                    P->Offset <= Bounds - int(sizeof(float))) Offset = BaseOffset + P->Offset;
                else if (Type == "DoubleProperty" && P->ElementSize == sizeof(double) &&
                    P->Offset <= Bounds - int(sizeof(double))) { Offset = BaseOffset + P->Offset; IsDouble = true; }
                else return;
                Original = Applied = Read(O);
            }
            if (Offset < 0) return;
            const double Current = Read(O);
            if (!std::isfinite(Current)) { Restore(); return; }
            if (Current != Applied) Original = Current;
            Applied = Multiply ? Original * Value : Value;
            if (!IsDouble && !Mask) Applied = static_cast<float>(Applied);
            if (!std::isfinite(Applied)) { Applied = Current; Restore(); return; }
            Write(O, Applied);
        }
    };
