struct GargantarAttackOverrides {
    ExtensionObject Owner;
    uint8_t* Data = nullptr;
    int Stride = 0, Offset = -1;
    float Original[9]{}, Applied[9]{};
    bool View(SDK::UObject* O, ReflectedStructArrayView& V) const {
        return GetReflectedStructArray(O,"AttackInfos",32,&V) && V.Num==9 &&
            V.ElementStruct && V.ElementStruct->GetName()=="DinoAttackInfo" &&
            V.ElementSize==V.ElementStruct->Size && V.ElementSize==304;
    }
    void Restore() {
        ReflectedStructArrayView V{};
        auto* O=Owner.Get();
        if (O && Offset>=0 && View(O,V) && V.Data==Data && V.ElementSize==Stride) {
            for (int I=0;I<9;++I) {
                auto* P=reinterpret_cast<float*>(Data+I*Stride+Offset);
                if (*P==Applied[I]) *P=Original[I];
            }
        }
        Owner={}; Data=nullptr; Stride=0; Offset=-1;
    }
    bool Set(SDK::UObject* O,float Scale) {
        if (!std::isfinite(Scale) || Scale<.1f || Scale>1) { Restore(); return false; }
        ReflectedStructArrayView V{};
        if (!View(O,V)) { Restore(); return false; }
        if (Owner.Get()!=O || Data!=V.Data || Stride!=V.ElementSize) {
            Restore();
            auto* P=FindReflectedProperty(V.ElementStruct,"AttackInterval");
            if (!P || !P->ClassPrivate || P->ClassPrivate->Name.ToString()!="FloatProperty" ||
                P->ArrayDim!=1 || P->ElementSize!=4 || P->Offset<0 || P->Offset>V.ElementSize-4) return false;
            Owner=ExtensionObject(O); if (!Owner.Get()) return false;
            Data=V.Data; Stride=V.ElementSize; Offset=P->Offset;
            for (int I=0;I<9;++I) {
                Original[I]=Applied[I]=*reinterpret_cast<float*>(Data+I*Stride+Offset);
                if (!std::isfinite(Original[I]) || Original[I]<0) { Offset=-1; Restore(); return false; }
            }
        }
        for (int I=0;I<9;++I) {
            auto* P=reinterpret_cast<float*>(Data+I*Stride+Offset);
            if (!std::isfinite(*P) || *P<0) { Restore(); return false; }
            if (*P!=Applied[I]) Original[I]=*P;
            Applied[I]=Original[I]*Scale; *P=Applied[I];
        }
        return true;
    }
};
