// Copyright (C) Developed by Pask, Published by Dark Tower Interactive SRL 2024. All Rights Reserved.

#include "Components/ACFEquipmentComponent.h"

#include <GameFramework/CharacterMovementComponent.h>
#include <GameplayTagContainer.h>
#include <Kismet/KismetSystemLibrary.h>
#include <NavigationSystem.h>

#include "ACFItemSystemFunctionLibrary.h"
#include "ARSStatisticsComponent.h"
#include "Components/ACFArmorSlotComponent.h"
#include "Components/ACFStorageComponent.h"
#include "GameFramework/Character.h"
#include "Items/ACFAccessory.h"
#include "Items/ACFArmor.h"
#include "Items/ACFConsumable.h"
#include "Items/ACFEquippableItem.h"
#include "Items/ACFItem.h"
#include "Items/ACFMeleeWeapon.h"
#include "Items/ACFProjectile.h"
#include "Items/ACFRangedWeapon.h"
#include "Items/ACFWeapon.h"
#include "Items/ACFWorldItem.h"
#include "Kismet/KismetMathLibrary.h"
#include "Net/UnrealNetwork.h"
#include <GameFramework/Actor.h>

void UACFEquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UACFEquipmentComponent, Equipment);
    DOREPLIFETIME(UACFEquipmentComponent, Inventory);
    DOREPLIFETIME(UACFEquipmentComponent, currentInventoryWeight);
    DOREPLIFETIME(UACFEquipmentComponent, CurrentlyEquippedSlotType);
}

// Sets default values for this component's properties
UACFEquipmentComponent::UACFEquipmentComponent()
{
    // Set this component to be initialized when the game starts, and to be ticked
    // every frame.  You can turn these features off to improve performance if you
    // don't need them.
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    Inventory.Empty();
}

void UACFEquipmentComponent::BeginPlay()
{
    Super::BeginPlay();

    GatherCharacterOwner();
}

void UACFEquipmentComponent::GatherCharacterOwner()
{
    if (!CharacterOwner) {
        CharacterOwner = Cast<ACharacter>(GetOwner());
    }
}

void UACFEquipmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{

    if (EndPlayReason == EEndPlayReason::RemovedFromWorld) {
        SheathCurrentWeapon();
    }
    Super::EndPlay(EndPlayReason);
}

void UACFEquipmentComponent::OnComponentLoaded_Implementation()
{
    DestroyEquipment();

    Equipment.EquippedItems.Empty();
    for (auto& slot : Inventory) {
        slot.RefreshDescriptor();
        UACFItemSystemFunctionLibrary::GetItemData(slot.ItemClass, slot.ItemInfo);
        if (slot.bIsEquipped) {
            EquipItemFromInventory(slot);
        }
    }
    RefreshTotalWeight();
}

void UACFEquipmentComponent::AddItemToInventory_Implementation(const FBaseItem& ItemToAdd, bool bAutoEquip)
{
    Internal_AddItem(ItemToAdd, bAutoEquip);
}

void UACFEquipmentComponent::AddItemToInventoryByClass_Implementation(TSubclassOf<AACFItem> inItem, int32 count /*= 1*/, bool bAutoEquip)
{
    AddItemToInventory(FBaseItem(inItem, count), bAutoEquip);
}

void UACFEquipmentComponent::RemoveItemByIndex_Implementation(const int32 index, int32 count /*= 1*/)
{
    if (Inventory.IsValidIndex(index)) {
        RemoveItem(Inventory[index], count);
    }
}

void UACFEquipmentComponent::DropItem_Implementation(const FInventoryItem& item, int32 count /*= 1*/)
{
    FInventoryItem* itemptr = Internal_GetInventoryItem(item);
    if (!itemptr) {
        return;
    }

    if (item.ItemInfo.bDroppable) {
        TArray<FBaseItem> toDrop;
        toDrop.Add(FBaseItem(item.ItemClass, count));
        SpawnWorldItem(toDrop);

        RemoveItem(item, count);
    }
}

void UACFEquipmentComponent::RemoveItem_Implementation(const FInventoryItem& item, int32 count /*= 1*/)
{
    FInventoryItem* itemptr = Internal_GetInventoryItem(item);
    if (itemptr) {
        const int32 finalCount = FMath::Min(count, itemptr->Count);
        const float weightRemoved = finalCount * itemptr->ItemInfo.ItemWeight;
        itemptr->Count -= finalCount;

        if (itemptr->Count <= 0) {
            if (itemptr->bIsEquipped) {
                FEquippedItem outItem;
                GetEquippedItemSlot(itemptr->EquipmentSlot, outItem);
                RemoveItemFromEquipment(outItem);
            }
            FInventoryItem toBeRemoved;
            if (GetItemByGuid(itemptr->GetItemGuid(), toBeRemoved)) {
                Inventory.Remove(toBeRemoved);
            }
        } else {
            if (itemptr->bIsEquipped && Equipment.EquippedItems.Contains(item.EquipmentSlot)) {
                const int32 index = Equipment.EquippedItems.IndexOfByKey(item.EquipmentSlot);
                Equipment.EquippedItems[index].InventoryItem.Count = itemptr->Count;
                RefreshEquipment();
                OnEquipmentChanged.Broadcast(Equipment);
            }
        }
        currentInventoryWeight -= weightRemoved;
        OnItemRemoved.Broadcast(FBaseItem(item.ItemClass, finalCount));

        OnInventoryChanged.Broadcast(Inventory);
    }
}

void UACFEquipmentComponent::UseInventoryItem_Implementation(const FInventoryItem& item)
{
    FInventoryItem invItem;
    if (GetItemByGuid(item.GetItemGuid(), invItem)) {
        if (!invItem.bIsEquipped) {
            EquipItemFromInventory(invItem);
        } else {
            UnequipItemBySlot(invItem.EquipmentSlot);
        }
    }
}

void UACFEquipmentComponent::UseInventoryItemByIndex_Implementation(int32 index)
{
    if (Inventory.IsValidIndex(index)) {
        FInventoryItem item = Inventory[index];
        UseInventoryItem(item);
    }
}

bool UACFEquipmentComponent::HasEnoughItemsOfType(const TArray<FBaseItem>& ItemsToCheck)
{
    for (const auto& item : ItemsToCheck) {
        int32 numberToCheck = item.Count;
        TArray<FInventoryItem*> invItems = FindItemsByClass(item.ItemClass);
        int32 TotItems = 0;
        for (const auto& invItem : invItems) {
            if (invItem) {
                TotItems += invItem->Count;
            }
        }

        if (TotItems < numberToCheck) {
            return false;
        }
    }
    return true;
}

FGameplayTag UACFEquipmentComponent::GetCurrentDesiredMovesetTag() const
{
    if (Equipment.SecondaryWeapon && Equipment.SecondaryWeapon->OverridesMainHandMoveset()) {
        return Equipment.SecondaryWeapon->GetAssociatedMovesetTag();
    } else if (Equipment.MainWeapon) {
        return Equipment.MainWeapon->GetAssociatedMovesetTag();
    }
    return FGameplayTag();
}

FGameplayTag UACFEquipmentComponent::GetCurrentDesiredMovesetActionTag() const
{
    if (Equipment.SecondaryWeapon && Equipment.SecondaryWeapon->OverridesMainHandMovesetActions()) {
        return Equipment.SecondaryWeapon->GetAssociatedMovesetActionsTag();
    } else if (Equipment.MainWeapon) {
        return Equipment.MainWeapon->GetAssociatedMovesetActionsTag();
    }
    return FGameplayTag();
}

FGameplayTag UACFEquipmentComponent::GetCurrentDesiredOverlayTag() const
{
    if (Equipment.SecondaryWeapon && Equipment.SecondaryWeapon->OverridesMainHandOverlay()) {
        return Equipment.SecondaryWeapon->GetAssociatedMovesetOverlayTag();
    } else if (Equipment.MainWeapon) {
        return Equipment.MainWeapon->GetAssociatedMovesetOverlayTag();
    }
    return FGameplayTag();
}

void UACFEquipmentComponent::ConsumeItems_Implementation(const TArray<FBaseItem>& ItemsToCheck)
{
    for (const auto& item : ItemsToCheck) {
        const TArray<FInventoryItem*> invItems = FindItemsByClass(item.ItemClass);
        if (invItems.IsValidIndex(0)) {
            RemoveItem(*(invItems[0]), item.Count);
        }
    }
}

void UACFEquipmentComponent::MoveItemsToInventory_Implementation(const TArray<FBaseItem>& inItems, UACFStorageComponent* storage)
{
    if (!storage) {
        UE_LOG(LogTemp, Error,
            TEXT("Invalid Storage, verify that the owner of this component is "
                 "repliacted! - ACFEquipmentComp"));
        return;
    }

    TArray<FBaseItem> pendingRemove;
    for (const auto& item : inItems) {
        const int32 numItems = UKismetMathLibrary::Min(NumberOfItemCanTake(item.ItemClass), item.Count);
        AddItemToInventoryByClass(item.ItemClass, numItems, true);
        pendingRemove.Add(FBaseItem(item.ItemClass, numItems));
    }
    storage->RemoveItems(pendingRemove);
}

void UACFEquipmentComponent::OnRep_Equipment()
{
    RefreshEquipment();
    OnEquipmentChanged.Broadcast(Equipment);
}

void UACFEquipmentComponent::RefreshEquipment()
{
    if (!CharacterOwner) {
        CharacterOwner = Cast<ACharacter>(GetOwner());
    }
    //     if (CharacterOwner) {
    //         MainCharacterMesh = CharacterOwner->GetMesh();
    //     }
    FillModularMeshes();
    for (const auto& item : Equipment.EquippedItems) {
        AACFEquippableItem* equippable = Cast<AACFEquippableItem>(item.Item);
        if (equippable) {
            AACFWeapon* WeaponToEquip = Cast<AACFWeapon>(equippable);
            if (WeaponToEquip) {
                if (WeaponToEquip == Equipment.MainWeapon || WeaponToEquip == Equipment.SecondaryWeapon) {
                    continue;
                } else {
                    AttachWeaponOnBody(WeaponToEquip);
                }
            }

            AACFArmor* ArmorToEquip = Cast<AACFArmor>(equippable);
            if (ArmorToEquip) {
                ArmorToEquip->SetActorHiddenInGame(true);
                AddSkeletalMeshComponent(ArmorToEquip->GetClass(),
                    item.ItemSlot);
            }
            AACFProjectile* proj = Cast<AACFProjectile>(equippable);
            if (proj) {
                proj->SetActorHiddenInGame(true);
            }

            AACFAccessory* itemToEquip = Cast<AACFAccessory>(equippable);
            if (itemToEquip) {
                itemToEquip->AttachToComponent(MainCharacterMesh, FAttachmentTransformRules::SnapToTargetIncludingScale, itemToEquip->GetAttachmentSocket());
            }
        }
    }
}

void UACFEquipmentComponent::RefreshTotalWeight()
{
    currentInventoryWeight = 0.f;
    for (const auto& item : Inventory) {
        currentInventoryWeight += item.ItemInfo.ItemWeight * item.Count;
    }
}

bool UACFEquipmentComponent::ShouldUseLeftHandIK() const
{
    if (Equipment.MainWeapon) {
        return Equipment.MainWeapon->IsUsingLeftHandIK();
    }
    return false;
}

FVector UACFEquipmentComponent::GetLeftHandIkPos() const
{
    if (Equipment.MainWeapon) {
        return Equipment.MainWeapon->GetLeftHandleIKPosition();
    }
    return FVector();
}

bool UACFEquipmentComponent::IsSlotAvailable(const FGameplayTag& itemSlot) const
{
    if (itemSlot == FGameplayTag()) {
        return false;
    }

    if (!UACFItemSystemFunctionLibrary::IsValidItemSlotTag(itemSlot)) {
        UE_LOG(LogTemp, Log,
            TEXT("Invalid item Slot Tag!!! -  "
                 "UACFEquipmentComponent::IsSlotAvailable"));
        return false;
    }
    return !Equipment.EquippedItems.Contains(itemSlot) && GetAvailableEquipmentSlot().Contains(itemSlot);
}

bool UACFEquipmentComponent::TryFindAvailableItemSlot(const TArray<FGameplayTag>& itemSlots, FGameplayTag& outAvailableSlot)
{
    for (const auto& slot : itemSlots) {
        if (IsSlotAvailable(slot)) {
            outAvailableSlot = slot;
            return true;
        }
    }
    return false;
}

bool UACFEquipmentComponent::HaveAtLeastAValidSlot(const TArray<FGameplayTag>& itemSlots)
{
    for (const auto& slot : itemSlots) {
        if (GetAvailableEquipmentSlot().Contains(slot)) {
            return true;
        }
    }
    return false;
}

void UACFEquipmentComponent::OnRep_Inventory()
{
    OnInventoryChanged.Broadcast(Inventory);
}

void UACFEquipmentComponent::FillModularMeshes()
{
    TArray<UACFArmorSlotComponent*> slots;
    GetOwner()->GetComponents<UACFArmorSlotComponent>(slots, false);
    ModularMeshes.Empty();
    for (const auto slot : slots) {
        ModularMeshes.Add(FModularPart(slot));
        slot->SetLeaderPoseComponent(MainCharacterMesh);
    }
}

void UACFEquipmentComponent::OnEntityOwnerDeath_Implementation()
{
    if (CharacterOwner && bDropItemsOnDeath) {
        TArray<AActor*> attachedActors;
        CharacterOwner->GetAttachedActors(attachedActors, true);
        TArray<FBaseItem> projCount;
        for (const auto& actor : attachedActors) {
            if (IsValid(actor) && !actor->IsPendingKillPending()) {
                AACFProjectile* proj = Cast<AACFProjectile>(actor);
                if (IsValid(proj)) {
                    const float percentage = FMath::RandRange(0.f, 100.f);
                    if (proj->ShouldBeDroppedOnDeath() && proj->GetDropOnDeathPercentage() >= percentage) {
                        if (projCount.Contains(proj->GetClass())) {
                            FBaseItem* actualItem = projCount.FindByKey(proj->GetClass());
                            actualItem->Count += 1;
                        } else {
                            projCount.Add(FBaseItem(proj->GetClass(), 1));
                        }
                    }
                    proj->SetLifeSpan(0.2f);
                }
            }
        }
        UACFItemSystemFunctionLibrary::SpawnWorldItemNearLocation(this, projCount, CharacterOwner->GetCharacterMovement()->GetActorFeetLocation(), 100.f);
    }
    if (bDestroyItemsOnDeath) {
        DestroyEquippedItems();
    }
}

void UACFEquipmentComponent::Internal_OnArmorUnequipped_Implementation(const FGameplayTag& slot)
{
    FModularPart outMesh;
    FEquippedItem outEquip;

    if (GetModularMesh(slot, outMesh) && outMesh.meshComp) {
        outMesh.meshComp->ResetSlotToEmpty();
        OnEquippedArmorChanged.Broadcast(slot);
    }
}

bool UACFEquipmentComponent::CanBeEquipped(const TSubclassOf<AACFItem>& equippable)
{
    FItemDescriptor ItemData;
    TArray<FAttribute> attributes;
    UACFItemSystemFunctionLibrary::GetItemData(equippable, ItemData);

    GatherCharacterOwner();
    if (!HaveAtLeastAValidSlot(ItemData.GetPossibleItemSlots())) {
        UE_LOG(LogTemp, Log, TEXT("No VALID item slots! Impossible to equip! - ACFEquipmentComp"));
        return false;
    }
    if (UACFItemSystemFunctionLibrary::GetEquippableAttributeRequirements(equippable, attributes)) {
        const UARSStatisticsComponent* statcomp = CharacterOwner->FindComponentByClass<UARSStatisticsComponent>();
        if (statcomp) {
            return statcomp->CheckPrimaryAttributesRequirements(attributes);
        } else {
            UE_LOG(LogTemp, Log,
                TEXT("Add UARSStatisticsComponent to your character!! - ACFEquipmentComp"));
        }
    } else {
        return true;
    }
    return false;
}

int32 UACFEquipmentComponent::Internal_AddItem(const FBaseItem& itemToAdd, bool bTryToEquip /*= true*/, float dropChancePercentage /*= 0.f*/)
{
    int32 addeditemstotal = 0;
    int32 addeditemstmp = 0;
    bool bSuccessful = false;
    FItemDescriptor itemData;

    UACFItemSystemFunctionLibrary::GetItemData(itemToAdd.ItemClass, itemData);

    if (itemData.MaxInventoryStack == 0) {
        UE_LOG(LogTemp, Warning,
            TEXT("Max Inventory Stack cannot be 0!!!! - UACFEquipmentComponent::Internal_AddItem"));
        return -1;
    }

    if (currentInventoryWeight >= MaxInventoryWeight) {
        return -1;
    }
    const int32 itemweight = itemData.ItemWeight;
    int32 maxAddableByWeightTotal = itemToAdd.Count;
    if (itemweight > 0) {
        maxAddableByWeightTotal = FMath::TruncToInt(
            (MaxInventoryWeight - currentInventoryWeight) / itemweight);
    }
    int32 count = itemToAdd.Count;
    if (maxAddableByWeightTotal < itemToAdd.Count) {
        count = maxAddableByWeightTotal;
    }

    if (count <= 0) {
        return -1;
    }
    int32 MaxInventoryStack = itemData.MaxInventoryStack;
    TArray<FInventoryItem*> outItems = FindItemsByClass(itemToAdd.ItemClass);
    // IF WE ALREADY HAVE SOME ITEMS LIKE THAT, INCREMENT ACTUAL VALUE

    bool bGate = true;
    if (outItems.Num() > 0) {
        for (const auto& outItem : outItems) {
            if (outItem->Count < itemData.MaxInventoryStack) {
                if (outItem->Count + count <= itemData.MaxInventoryStack && count * itemData.ItemWeight + currentInventoryWeight <= MaxInventoryWeight) {
                    addeditemstmp = count;
                } else {
                    int32 maxAddableByStack = itemData.MaxInventoryStack - outItem->Count;
                    addeditemstmp = maxAddableByStack;
                }

                outItem->Count += addeditemstmp;
                addeditemstotal += addeditemstmp;
                count -= addeditemstmp;
                outItem->DropChancePercentage = dropChancePercentage;
                if (outItem->bIsEquipped && Equipment.EquippedItems.Contains(outItem->EquipmentSlot)) {
                    int32 index = Equipment.EquippedItems.IndexOfByKey(outItem->EquipmentSlot);
                    Equipment.EquippedItems[index].InventoryItem.Count = outItem->Count;
                    OnEquipmentChanged.Broadcast(Equipment);
                } else if (bTryToEquip && !Equipment.EquippedItems.Contains(outItem->EquipmentSlot)) {
                    EquipItemFromInventory(*outItem);
                }
                bSuccessful = true;
                // break;
            }
        }
    }

    // Otherwise we add new
    const int32 NumberOfItemNeed = FMath::CeilToInt((float)count / (float)itemData.MaxInventoryStack);
    const int32 FreeSpaceInInventory = MaxInventorySlots - Inventory.Num();
    const int32 NumberOfStackToCreate = FGenericPlatformMath::Min(NumberOfItemNeed, FreeSpaceInInventory);
    for (int i = 0; i < NumberOfStackToCreate; i++) {
        if (Inventory.Num() < MaxInventorySlots) {
            FInventoryItem newItem(itemToAdd);
            if (count > itemData.MaxInventoryStack) {
                newItem.Count = itemData.MaxInventoryStack;
            } else {
                newItem.Count = count;
            }
            newItem.DropChancePercentage = dropChancePercentage;

            newItem.InventoryIndex = GetFirstEmptyInventoryIndex();
            addeditemstotal += newItem.Count;
            count -= newItem.Count;
            Inventory.Add(newItem);
            FGameplayTag outTag;
            if (bTryToEquip && TryFindAvailableItemSlot(newItem.ItemInfo.GetPossibleItemSlots(), outTag)) {
                EquipItemFromInventory(newItem);
            }
            bSuccessful = true;
        }
    }
    if (bSuccessful) {
        currentInventoryWeight += itemData.ItemWeight * addeditemstotal;
        OnInventoryChanged.Broadcast(Inventory);
        if (addeditemstotal > 0) {
            OnItemAdded.Broadcast(FBaseItem(itemToAdd.ItemClass, addeditemstotal));
        }
        return addeditemstotal;
    }

    return addeditemstotal;
}

void UACFEquipmentComponent::SetInventoryItemSlotIndex_Implementation(const FInventoryItem& item, int newIndex)
{
    if (newIndex < MaxInventorySlots) {
        if (Inventory.Contains(item)) {
            FInventoryItem* invItem = Internal_GetInventoryItem(item);
            if (invItem->InventoryIndex != newIndex) {
                if (IsSlotEmpty(newIndex))
                    invItem->InventoryIndex = newIndex;
                else {
                    FInventoryItem itemTemp;
                    if (GetItemByInventoryIndex(newIndex, itemTemp)) {
                        FInventoryItem* itemDestination = Internal_GetInventoryItem(itemTemp);
                        itemDestination->InventoryIndex = invItem->InventoryIndex;
                        invItem->InventoryIndex = newIndex;
                    }
                }
            }
        }
    }
}

TArray<FInventoryItem*> UACFEquipmentComponent::FindItemsByClass(const TSubclassOf<AACFItem>& itemToFind)
{
    TArray<FInventoryItem*> foundItems;

    for (int32 i = 0; i < Inventory.Num(); i++) {
        if (Inventory[i].ItemClass == itemToFind) {
            foundItems.Add(&Inventory[i]);
        }
    }
    return foundItems;
}

void UACFEquipmentComponent::BeginDestroy()
{
    // Internal_DestroyEquipment();
    Super::BeginDestroy();
}

void UACFEquipmentComponent::AttachWeaponOnBody(AACFWeapon* WeaponToEquip)
{
    if (MainCharacterMesh) {
        const FName socket = WeaponToEquip->GetOnBodySocketName();

        if (socket != NAME_None) {
            WeaponToEquip->AttachToComponent(MainCharacterMesh, FAttachmentTransformRules::SnapToTargetIncludingScale, socket);
            WeaponToEquip->Internal_OnWeaponSheathed();
        } else {
            UE_LOG(LogTemp, Log, TEXT("Remember to setup sockets in your weapon! - ACFEquipmentComp"));
        }
    }
}

void UACFEquipmentComponent::AttachWeaponOnHand(AACFWeapon* localWeapon)
{
    const FName socket = localWeapon->GetEquippedSocketName();
    if (socket != NAME_None) {
        localWeapon->AttachToComponent(MainCharacterMesh, FAttachmentTransformRules::SnapToTargetIncludingScale, socket);
        localWeapon->Internal_OnWeaponUnsheathed();
    } else {
        UE_LOG(LogTemp, Log, TEXT("Remember to setup sockets in your weapon! - ACFEquipmentComp"));
    }
}

void UACFEquipmentComponent::AddSkeletalMeshComponent_Implementation(TSubclassOf<class AACFArmor> ArmorClass, FGameplayTag itemSlot)
{
    if (!CharacterOwner) {
        return;
    }
    FModularPart outMesh;
    const AACFArmor* ArmorToAdd = Cast<AACFArmor>(ArmorClass->GetDefaultObject());

    if (!ArmorToAdd || !ArmorToAdd->GetArmorMesh()) {
        UE_LOG(LogTemp, Error, TEXT("Trying to wear an armor without armor mesh!!! - ACFEquipmentComp"));
        return;
    }

    if (GetModularMesh(itemSlot, outMesh) && outMesh.meshComp) {
        outMesh.meshComp->EmptyOverrideMaterials();
        outMesh.meshComp->SetSkinnedAssetAndUpdate(ArmorToAdd->GetArmorMesh());
        outMesh.meshComp->SetVisibility(true);
        outMesh.meshComp->bUseBoundsFromLeaderPoseComponent = true;
        outMesh.meshComp->AttachToComponent(MainCharacterMesh, FAttachmentTransformRules(EAttachmentRule::SnapToTarget, true));
        outMesh.meshComp->SetLeaderPoseComponent(MainCharacterMesh);

    } else {
        UACFArmorSlotComponent* NewComp = NewObject<UACFArmorSlotComponent>(CharacterOwner, itemSlot.GetTagName());

        NewComp->RegisterComponent();
        NewComp->SetWorldLocation(FVector::ZeroVector);
        NewComp->SetWorldRotation(FRotator::ZeroRotator);
        NewComp->SetSlotTag(itemSlot);
        NewComp->AttachToComponent(MainCharacterMesh, FAttachmentTransformRules(EAttachmentRule::SnapToTarget, true));
        NewComp->SetSkinnedAssetAndUpdate(ArmorToAdd->GetArmorMesh());
        NewComp->SetLeaderPoseComponent(MainCharacterMesh);
        NewComp->bUseBoundsFromLeaderPoseComponent = true;

        ModularMeshes.Add(FModularPart(NewComp));
    }
    OnEquippedArmorChanged.Broadcast(itemSlot);
}

void UACFEquipmentComponent::UseEquippedItemBySlot_Implementation(FGameplayTag ItemSlot)
{
    if (!UACFItemSystemFunctionLibrary::IsValidItemSlotTag(ItemSlot)) {
        UE_LOG(LogTemp, Log, TEXT("Invalid item Slot Tag!!! - ACFEquipmentComp"));
        return;
    }

    if (ItemSlot == CurrentlyEquippedSlotType) {
        SheathCurrentWeapon();
        return;
    }

    FEquippedItem EquipSlot;

    if (GetEquippedItemSlot(ItemSlot, EquipSlot)) {
        AACFWeapon* localWeapon = Cast<AACFWeapon>(EquipSlot.Item);
        if (localWeapon) {
            if (localWeapon->GetHandleType() == EHandleType::OffHand) {
                if (Equipment.MainWeapon && Equipment.MainWeapon->GetHandleType() == EHandleType::OneHanded) {
                    Equipment.SecondaryWeapon = localWeapon;
                } else {
                    UE_LOG(LogTemp, Log, TEXT("Impossible to equip Offhand! - ACFEquipmentComp"));
                    return;
                }
            } else {
                if (Equipment.MainWeapon) {
                    SheathCurrentWeapon();
                }
                Equipment.MainWeapon = localWeapon;
                CurrentlyEquippedSlotType = ItemSlot;
            }
            AttachWeaponOnHand(localWeapon);
        } else if (EquipSlot.Item && EquipSlot.Item->IsA(AACFConsumable::StaticClass())) {
            UseEquippedConsumable(EquipSlot, CharacterOwner);
        }
        OnEquipmentChanged.Broadcast(Equipment);
    }
}

void UACFEquipmentComponent::UnequipItemBySlot_Implementation(FGameplayTag itemSlot)
{
    if (!UACFItemSystemFunctionLibrary::IsValidItemSlotTag(itemSlot)) {
        UE_LOG(LogTemp, Log, TEXT("Invalid item Slot Tag!!! - ACFEquipmentComp"));
        return;
    }

    if (itemSlot == CurrentlyEquippedSlotType) {
        SheathCurrentWeapon();
    }

    FEquippedItem EquipSlot;
    if (GetEquippedItemSlot(itemSlot, EquipSlot)) {
        RemoveItemFromEquipment(EquipSlot);
    }
}

void UACFEquipmentComponent::UnequipItemByGuid_Implementation(const FGuid& itemGuid)
{
    FEquippedItem EquipSlot;
    if (GetEquippedItem(itemGuid, EquipSlot)) {
        RemoveItemFromEquipment(EquipSlot);
    }
}

void UACFEquipmentComponent::SheathCurrentWeapon_Implementation()
{
    if (Equipment.MainWeapon) {
        FName socket = Equipment.MainWeapon->GetOnBodySocketName();
        if (socket != NAME_None) {
            Equipment.MainWeapon->AttachToComponent(MainCharacterMesh, FAttachmentTransformRules::SnapToTargetIncludingScale, socket);
            Equipment.MainWeapon->Internal_OnWeaponSheathed();
            Equipment.MainWeapon = nullptr;
        } else {
            UE_LOG(LogTemp, Log, TEXT("Remember to setup sockets in your weapon! - ACFEquipmentComp"));
        }

        if (Equipment.SecondaryWeapon) {
            socket = Equipment.SecondaryWeapon->GetOnBodySocketName();
            if (socket != NAME_None) {
                Equipment.SecondaryWeapon->AttachToComponent(MainCharacterMesh, FAttachmentTransformRules::SnapToTargetIncludingScale, socket);
                Equipment.SecondaryWeapon->Internal_OnWeaponSheathed();
                Equipment.SecondaryWeapon = nullptr;
            } else {
                UE_LOG(LogTemp, Log,
                    TEXT("Remember to setup sockets in your weapon! - "
                         "ACFEquipmentComp"));
            }
        }
        Equipment.SecondaryWeapon = nullptr;
    }

    OnEquipmentChanged.Broadcast(Equipment);
    CurrentlyEquippedSlotType = UACFItemSystemFunctionLibrary::GetItemSlotTagRoot();
}

void UACFEquipmentComponent::EquipItemFromInventory_Implementation(const FInventoryItem& inItem)
{
    EquipItemFromInventoryInSlot(inItem, FGameplayTag());
}

void UACFEquipmentComponent::EquipItemFromInventoryInSlot_Implementation(const FInventoryItem& inItem, FGameplayTag slot)
{
    FInventoryItem item;

    if (Inventory.Contains(inItem.GetItemGuid())) {
        item = *(Internal_GetInventoryItemByGuid(inItem.GetItemGuid()));
    } else {
        return;
    }

    if (!CanBeEquipped(item.ItemClass)) {
        UE_LOG(LogTemp, Warning, TEXT("Item is not equippable  - ACFEquipmentComp"));
        return;
    }

    if (item.bIsEquipped) {

        ensure(item.EquipmentSlot != FGameplayTag());
        FEquippedItem currentItem;
        if (slot == FGameplayTag()) {
            slot = item.EquipmentSlot;
        }
        if (GetEquippedItemSlot(slot, currentItem) && currentItem.InventoryItem.GetItemGuid() == inItem.GetItemGuid()) {
            // already equipped in the same slot
            return;
        } else {
            // otherwise let's swap slot
            UnequipItemByGuid(inItem.GetItemGuid());
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    SpawnParams.Instigator = CharacterOwner;
    SpawnParams.OverrideLevel = CharacterOwner->GetLevel();
    UWorld* world = GetWorld();

    if (!world) {
        return;
    }

    AACFItem* itemInstance = world->SpawnActor<AACFItem>(item.ItemClass, CharacterOwner->GetActorLocation(), FRotator(0), SpawnParams);

    if (!itemInstance) {
        UE_LOG(LogTemp, Error, TEXT("Impossible to spawn item!!! - ACFEquipmentComp"));
        return;
    }
    itemInstance->SetItemOwner(CharacterOwner);
    
    AACFEquippableItem* equippable = Cast<AACFEquippableItem>(itemInstance);
    if (equippable && !equippable->CanBeEquipped(this)) {
        equippable->Destroy();
        return;
    }
    FGameplayTag selectedSlot;
    if (slot == FGameplayTag()) {
        if (!TryFindAvailableItemSlot(item.ItemInfo.ItemSlots, selectedSlot) && item.ItemInfo.ItemSlots.Num() > 0) {
            selectedSlot = item.ItemInfo.ItemSlots[0];
        }
    } else if (itemInstance->GetPossibleItemSlots().Contains(slot)) {
        selectedSlot = slot;
    } else {
        UE_LOG(LogTemp, Error, TEXT("Trying to equip an item in to an invalid Slot!!! - ACFEquipmentComp"));
        return;
    }

    UnequipItemBySlot(selectedSlot);

    if (equippable) {
        equippable->Internal_OnEquipped(CharacterOwner);
    } else {
        //necessary to avoid garbage collection of actors
        FAttachmentTransformRules defaultRules = FAttachmentTransformRules::SnapToTargetNotIncludingScale;

        itemInstance->AttachToActor(CharacterOwner, defaultRules);
    }
    Equipment.EquippedItems.Add(FEquippedItem(item, selectedSlot, itemInstance));
    MarkItemOnInventoryAsEquipped(item, true, selectedSlot);

    RefreshEquipment();
    OnEquipmentChanged.Broadcast(Equipment);
}

void UACFEquipmentComponent::DropItemByInventoryIndex_Implementation(int32 itemIndex, int32 count)
{
    if (Inventory.IsValidIndex(itemIndex)) {
        DropItem(Inventory[itemIndex], count);
    }
}

void UACFEquipmentComponent::RemoveItemFromEquipment(const FEquippedItem& equippedItem)
{
    //     if (!equippedItem.Item) {
    //         return;
    //     }

    const int32 index = Equipment.EquippedItems.IndexOfByKey(equippedItem.GetItemSlot());
    MarkItemOnInventoryAsEquipped(equippedItem.InventoryItem, false, FGameplayTag());
    if (equippedItem.Item->IsValidLowLevelFast()) {
        AACFEquippableItem* equippable = Cast<AACFEquippableItem>(equippedItem.Item);

        if (equippable) {
            equippable->Internal_OnUnEquipped();
            if (equippable->IsA(AACFArmor::StaticClass())) {
                Internal_OnArmorUnequipped(equippedItem.GetItemSlot());
            }
        }
        equippedItem.Item->Destroy();
    }

    Equipment.EquippedItems.RemoveAt(index);
    RefreshEquipment();
    OnEquipmentChanged.Broadcast(Equipment);
}

void UACFEquipmentComponent::MarkItemOnInventoryAsEquipped(const FInventoryItem& item, bool bIsEquipped, const FGameplayTag& itemSlot)
{
    FInventoryItem* itemstruct = Internal_GetInventoryItem(item);
    if (itemstruct) {
        itemstruct->bIsEquipped = bIsEquipped;
        itemstruct->EquipmentSlot = itemSlot;
    }
}

FInventoryItem* UACFEquipmentComponent::Internal_GetInventoryItem(const FInventoryItem& item)
{
    return Internal_GetInventoryItemByGuid(item.GetItemGuid());
}

FInventoryItem* UACFEquipmentComponent::Internal_GetInventoryItemByGuid(const FGuid& itemToSearch)
{
    return Inventory.FindByKey(itemToSearch);
}

FVector UACFEquipmentComponent::GetMainWeaponSocketLocation() const
{
    AACFRangedWeapon* rangedWeap = Cast<AACFRangedWeapon>(GetCurrentMainWeapon());
    if (rangedWeap) {
        return rangedWeap->GetShootingSocket();
    }

    return FVector();
}

bool UACFEquipmentComponent::GetItemByGuid(const FGuid& itemGuid, FInventoryItem& outItem) const
{
    if (Inventory.Contains(itemGuid)) {
        outItem = *Inventory.FindByKey(itemGuid);
        return true;
    }
    return false;
}

int32 UACFEquipmentComponent::GetTotalCountOfItemsByClass(const TSubclassOf<AACFItem>& ItemClass) const
{
    int32 totalItems = 0;
    TArray<FInventoryItem> outItems;
    GetAllItemsOfClassInInventory(ItemClass, outItems);

    for (const auto& item : outItems) {
        totalItems += item.Count;
    }
    return totalItems;
}

void UACFEquipmentComponent::GetAllItemsOfClassInInventory(const TSubclassOf<AACFItem>& ItemClass, TArray<FInventoryItem>& outItems) const
{
    outItems.Empty();
    for (int32 i = 0; i < Inventory.Num(); i++) {
        if (Inventory[i].ItemClass == ItemClass) {
            outItems.Add(Inventory[i]);
        }
    }
}

void UACFEquipmentComponent::GetAllSellableItemsInInventory(TArray<FInventoryItem>& outItems) const
{
    outItems.Empty();
    for (const auto& item : Inventory) {
        if (item.ItemInfo.bSellable) {
            outItems.Add(item);
        }
    }
}

bool UACFEquipmentComponent::FindFirstItemOfClassInInventory(const TSubclassOf<AACFItem>& ItemClass, FInventoryItem& outItem) const
{
    for (int32 i = 0; i < Inventory.Num(); i++) {
        if (Inventory[i].ItemClass == ItemClass) {
            outItem = Inventory[i];
            return true;
        }
    }
    return false;
}

bool UACFEquipmentComponent::GetEquippedItemSlot(const FGameplayTag& itemSlot, FEquippedItem& outSlot) const
{
    if (Equipment.EquippedItems.Contains(itemSlot)) {
        const int32 index = Equipment.EquippedItems.IndexOfByKey(itemSlot);
        outSlot = Equipment.EquippedItems[index];
        return true;
    }
    return false;
}

bool UACFEquipmentComponent::GetEquippedItem(const FGuid& itemGuid, FEquippedItem& outSlot) const
{
    if (Equipment.EquippedItems.Contains(itemGuid)) {
        const int32 index = Equipment.EquippedItems.IndexOfByKey(itemGuid);
        outSlot = Equipment.EquippedItems[index];
        return true;
    }
    return false;
}

bool UACFEquipmentComponent::GetModularMesh(FGameplayTag itemSlot, FModularPart& outMesh) const
{
    const FModularPart* slot = ModularMeshes.FindByKey(itemSlot);
    if (slot) {
        outMesh = *slot;
        return true;
    }
    return false;
}

bool UACFEquipmentComponent::HasAnyItemInEquipmentSlot(FGameplayTag itemSlot) const
{
    return Equipment.EquippedItems.Contains(itemSlot);
}

void UACFEquipmentComponent::UseConsumableOnActorBySlot_Implementation(FGameplayTag itemSlot, ACharacter* target)
{
    if (!UACFItemSystemFunctionLibrary::IsValidItemSlotTag(itemSlot)) {
        UE_LOG(LogTemp, Log, TEXT("Invalid item Slot Tag!!! - ACFEquipmentComp"));
        return;
    }

    FEquippedItem EquipSlot;
    if (GetEquippedItemSlot(itemSlot, EquipSlot)) {
        UseEquippedConsumable(EquipSlot, target);
    }
}

void UACFEquipmentComponent::SetDamageActivation(bool isActive, const TArray<FName>& traceChannels, bool isSecondaryWeapon /*= false*/)
{
    AACFMeleeWeapon* weapon;
    if (isSecondaryWeapon && Equipment.SecondaryWeapon) {
        weapon = Cast<AACFMeleeWeapon>(Equipment.SecondaryWeapon);
    } else if (!isSecondaryWeapon && Equipment.MainWeapon) {
        weapon = Cast<AACFMeleeWeapon>(Equipment.MainWeapon);
    } else {
        return;
    }

    if (weapon) {
        if (isActive) {
            weapon->StartWeaponSwing(traceChannels);
        } else {
            weapon->StopWeaponSwing();
        }
    }
}

void UACFEquipmentComponent::SetMainMesh(USkeletalMeshComponent* newMesh, bool bRefreshEquipment)
{
    MainCharacterMesh = newMesh;
    if (bRefreshEquipment) {
        RefreshEquipment();
    }
}

bool UACFEquipmentComponent::CanSwitchToRanged()
{
    for (const auto& weap : Equipment.EquippedItems) {
        if (weap.Item->IsA(AACFRangedWeapon::StaticClass())) {
            return true;
        }
    }
    return false;
}

bool UACFEquipmentComponent::CanSwitchToMelee()
{
    for (const auto& weap : Equipment.EquippedItems) {
        if (weap.Item->IsA(AACFMeleeWeapon::StaticClass())) {
            return true;
        }
    }
    return false;
}

bool UACFEquipmentComponent::HasOnBodyAnyWeaponOfType(TSubclassOf<AACFWeapon> weaponClass) const
{
    for (const auto& weapon : Equipment.EquippedItems) {
        if (weapon.Item->IsA(weaponClass)) {
            return true;
        }
    }
    return false;
}

void UACFEquipmentComponent::InitializeInventoryAndEquipment(USkeletalMeshComponent* inMainMesh)
{
    CharacterOwner = Cast<ACharacter>(GetOwner());
    SetMainMesh(inMainMesh, false);
    if (GetOwner()->HasAuthority()) {
        Inventory.Empty();
        currentInventoryWeight = 0.f;
        for (const FStartingItem& item : StartingItems) {
            Internal_AddItem(item, item.bAutoEquip, item.DropChancePercentage);
            if (Inventory.Num() > MaxInventorySlots) {
                UE_LOG(LogTemp, Log, TEXT("Invalid Inventory setup, too many slots on character!!! - "
                                          "ACFEquipmentComp"));
            }
        }
    }
}

void UACFEquipmentComponent::SpawnWorldItem(const TArray<FBaseItem>& items)
{
    if (CharacterOwner) {
        const FVector startLoc = CharacterOwner->GetNavAgentLocation();
        UACFItemSystemFunctionLibrary::SpawnWorldItemNearLocation(this, items, startLoc);
    }
}

void UACFEquipmentComponent::UseEquippedConsumable(FEquippedItem& EquipSlot, ACharacter* target)
{
    if (EquipSlot.Item->IsA(AACFConsumable::StaticClass())) {
        AACFConsumable* consumable = Cast<AACFConsumable>(EquipSlot.Item);

        Internal_UseItem(consumable, target, EquipSlot.InventoryItem);
    }
}

void UACFEquipmentComponent::UseConsumableOnTarget(const FInventoryItem& Inventoryitem, ACharacter* target)
{
    AACFConsumable* consumable = GetWorld()->SpawnActor<AACFConsumable>(Inventoryitem.ItemClass, FVector(0.f), FRotator(0));
    if (consumable->CanBeUsed(CharacterOwner)) {
        consumable->SetItemOwner(CharacterOwner);
        Internal_UseItem(consumable, target, Inventoryitem);
    }

    consumable->SetLifeSpan(.2f);
}

bool UACFEquipmentComponent::CanUseConsumable(const FInventoryItem& Inventoryitem)
{
    AACFConsumable* consumable = GetWorld()->SpawnActor<AACFConsumable>(Inventoryitem.ItemClass, FVector(0.f), FRotator(0));
    consumable->SetLifeSpan(.2f);
    return consumable && consumable->CanBeUsed(CharacterOwner);
}

void UACFEquipmentComponent::Internal_UseItem(AACFConsumable* consumable, ACharacter* target, const FInventoryItem& Inventoryitem)
{
    if (consumable && consumable->CanBeUsed(CharacterOwner)) {

        consumable->Internal_UseItem(target);
        if (consumable->bConsumeOnUse) {
            RemoveItem(Inventoryitem, 1);
        }
    } else {
        UE_LOG(LogTemp, Error, TEXT("Invalid Consumable!!! - "
                                    "UACFEquipmentComponent::UseConsumableOnTarget"));
    }
}

void UACFEquipmentComponent::DestroyEquipment()
{
    for (auto& equip : Equipment.EquippedItems) {
        AACFEquippableItem* equippable = Cast<AACFEquippableItem>(equip.Item);
        if (equippable) {
            equippable->Internal_OnUnEquipped();
        }
        equip.Item->Destroy();
    }
}

void UACFEquipmentComponent::DestroyEquippedItems_Implementation()
{
    Internal_DestroyEquipment();

    TArray<FBaseItem> toDrop;
    if (bDropItemsOnDeath && Inventory.Num() > 0) {
        for (int32 Index = Inventory.Num() - 1; Index >= 0; --Index) {
            if (Inventory.IsValidIndex(Index) && Inventory[Index].ItemInfo.bDroppable) {
                FBaseItem newItem(Inventory[Index]);
                newItem.Count = 0;

                for (uint8 i = 0; i < Inventory[Index].Count; i++) {
                    if (Inventory[Index].DropChancePercentage > FMath::RandRange(0.f, 100.f)) {
                        newItem.Count++;
                    }
                }

                if (newItem.Count > 0) {
                    toDrop.Add(newItem);
                    if (!bCollapseDropInASingleWorldItem) {
                        TArray<FBaseItem> newDrop;
                        newDrop.Add(newItem);
                        SpawnWorldItem(newDrop);

                        RemoveItem(Inventory[Index], Inventory[Index].Count);
                    } else {
                        RemoveItem(Inventory[Index], Inventory[Index].Count);
                    }
                }
            }
        }

        if (bCollapseDropInASingleWorldItem) {
            SpawnWorldItem(toDrop);
        }
    }
}

void UACFEquipmentComponent::Internal_DestroyEquipment()
{
    for (auto& weap : Equipment.EquippedItems) {
        if (weap.Item) {
            AACFEquippableItem* equippable = Cast<AACFEquippableItem>(weap.Item);
            if (equippable) {
                equippable->Internal_OnUnEquipped();
            }
            weap.Item->SetLifeSpan(.1f);
        }
    }
}

int32 UACFEquipmentComponent::NumberOfItemCanTake(const TSubclassOf<AACFItem>& itemToCheck)
{
    int32 addeditemstotal = 0;
    TArray<FInventoryItem*> outItems = FindItemsByClass(itemToCheck);
    FItemDescriptor itemInfo;
    UACFItemSystemFunctionLibrary::GetItemData(itemToCheck, itemInfo);
    float MaxByWeight = 999.f;
    if (itemInfo.ItemWeight > 0) {
        MaxByWeight = (MaxInventoryWeight - currentInventoryWeight) / itemInfo.ItemWeight;
    }
    const int32 maxAddableByWeight = FMath::TruncToInt(MaxByWeight);
    const int32 FreeSpaceInInventory = MaxInventorySlots - Inventory.Num();
    int32 maxAddableByStack = FreeSpaceInInventory * itemInfo.MaxInventoryStack;
    // IF WE ALREADY HAVE SOME ITEMS LIKE THAT, INCREMENT ACTUAL VALUE
    if (outItems.Num() > 0) {
        for (const auto& outItem : outItems) {
            maxAddableByStack += itemInfo.MaxInventoryStack - outItem->Count;
        }
    }
    addeditemstotal = FGenericPlatformMath::Min(maxAddableByStack, maxAddableByWeight);
    return addeditemstotal;
}

FInventoryItem::FInventoryItem(const FBaseItem& inItem)
{
    Count = inItem.Count;
    UACFItemSystemFunctionLibrary::GetItemData(inItem.ItemClass, ItemInfo);
    ItemGuid = FGuid::NewGuid();
    ItemClass = inItem.ItemClass.Get();
}

FInventoryItem::FInventoryItem(const FStartingItem& inItem)
{
    Count = inItem.Count;
    UACFItemSystemFunctionLibrary::GetItemData(inItem.ItemClass, ItemInfo);
    ItemGuid = FGuid::NewGuid();
    ItemClass = inItem.ItemClass.Get();
    DropChancePercentage = inItem.DropChancePercentage;
};

void FInventoryItem::RefreshDescriptor()
{
    UACFItemSystemFunctionLibrary::GetItemData(ItemClass, ItemInfo);
}

// void
// UACFEquipmentComponent::MoveItemToAnotherInventory_Implementation(UACFEquipmentComponent*
// OtherEquipmentComponent, class AACFItem* itemToMove, int32 count /*= 1*/)
// {
//     if (!itemToMove) {
//         return;
//     }
//     if (itemToMove->GetItemInfo().MaxInventoryStack > 1) {
//         int32 howmanycantake =
//         OtherEquipmentComponent->NumberOfItemCanTake(itemToMove); if
//         (howmanycantake < count) {
//             count = howmanycantake;
//         }
//         int NumberToRemove =
//         OtherEquipmentComponent->Internal_AddItem(itemToMove, count);
//         //int
//         NumberToRemove=OtherEquipmentComponent->AddItemToInventoryByClass(itemToMove->GetClass(),
//         count, false); RemoveItem(itemToMove, NumberToRemove);
//     } else {
//         int NumberToRemove =
//         OtherEquipmentComponent->Internal_AddItem(itemToMove, 1); if
//         (NumberToRemove == 1) {
//             RemoveItem(itemToMove);
//         }
//     }
// }

// bool
// UACFEquipmentComponent::MoveItemToAnotherInventory_Validate(UACFEquipmentComponent*
// OtherEquipmentComponent, class AACFItem* itemToMove, int32 count /*= 1*/)
// {
//     return true;
// }
